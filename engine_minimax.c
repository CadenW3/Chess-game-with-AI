/*
 * engine_minimax.c
 *
 * A practical, optimized depth-6 chess search core in C suitable for on-board firmware
 * (e.g., softcore/Zynq CPU controlling FPGA accelerators).
 *
 * What you get:
 * - Negamax + Alpha-Beta pruning
 * - Iterative deepening to depth=6 (good move ordering)
 * - Transposition table (Zobrist hash)
 * - Killer moves + history heuristic move ordering
 * - Quiescence search (captures/promotions) to reduce horizon blunders
 *
 * What you must plug in (because your exact HW + movegen contract isn’t provided):
 * - board_make_move() / board_unmake_move() (or do a copy-make per node)
 * - gen_moves() and gen_captures()  (can call your FPGA move generator)
 * - eval_position() (can call your FPGA DSP evaluator)
 *
 * This file is designed so the SEARCH is complete and optimized, while the
 * board/move generation/eval are clean “hooks” you replace with your own.
 *
 * Build target: embedded C (no malloc required).
 */

#include <stdint.h>
#include <string.h>

#ifndef MAX_MOVES
#define MAX_MOVES 256
#endif

#ifndef TT_SIZE_POW2
#define TT_SIZE_POW2 20              // 2^20 entries (~1,048,576)
#endif
#define TT_SIZE (1u << TT_SIZE_POW2)
#define TT_MASK (TT_SIZE - 1u)

#define INF  1000000000
#define MATE 999000000

// -------------------------------
// Move encoding (compact, fast)
// -------------------------------
// 0..5   from (0..63)
// 6..11  to   (0..63)
// 12..14 promo (0=None, 1=N,2=B,3=R,4=Q)
// 15     flags: capture bit (optional)
typedef uint16_t Move;

static inline Move MOVE_MAKE(uint8_t from, uint8_t to, uint8_t promo, uint8_t is_cap) {
    return (Move)((from & 63) | ((to & 63) << 6) | ((promo & 7) << 12) | ((is_cap & 1) << 15));
}
static inline uint8_t MOVE_FROM(Move m) { return (uint8_t)(m & 63); }
static inline uint8_t MOVE_TO(Move m)   { return (uint8_t)((m >> 6) & 63); }
static inline uint8_t MOVE_PROMO(Move m){ return (uint8_t)((m >> 12) & 7); }
static inline uint8_t MOVE_ISCAP(Move m){ return (uint8_t)((m >> 15) & 1); }

// -------------------------------
// Position / hooks you implement
// -------------------------------
typedef struct {
    // Put your board representation here.
    // Recommended on FPGA-side: bitboards + side-to-move + castling + ep + halfmove.
    // Example:
    // uint64_t bb[12];  // WP..WK, BP..BK
    // uint8_t  stm;     // 0 white, 1 black
    // uint8_t  castling;
    // int8_t   ep_sq;   // -1 if none
    // uint16_t ply;
    // uint64_t hash;
    uint64_t opaque0;
    uint64_t opaque1;
} Board;

typedef struct {
    Move m[MAX_MOVES];
    int  n;
} MoveList;

// Make/unmake. You can implement true unmake (stack) or “copy board per node”.
typedef struct {
    // store what you need to undo
    uint64_t s0, s1;
} Undo;

extern void board_make_move(Board *b, Move mv, Undo *u);
extern void board_unmake_move(Board *b, Move mv, const Undo *u);

// Legal move generation. You can route this to your FPGA move generator.
extern void gen_moves(const Board *b, MoveList *out);
extern void gen_captures(const Board *b, MoveList *out);

// Eval. You can route this to your FPGA DSP score block.
// Must return score from side-to-move perspective (negamax-friendly).
extern int eval_position(const Board *b);

// Terminal detection. Must be cheap and correct.
// If you don’t have checkmate detection yet, return 0 and you’ll still get a functioning engine,
// just weaker. Prefer: detect no-legal-moves + in_check => mate.
extern int board_is_in_check(const Board *b);
extern int board_is_drawish(const Board *b); // stalemate/insufficient/repetition/50-move etc. (optional)

// Zobrist hash: required for TT. Keep updated inside make/unmake for speed.
extern uint64_t board_hash(const Board *b);

// -------------------------------
// Transposition table
// -------------------------------
enum { TT_EXACT=0, TT_LOWER=1, TT_UPPER=2 };

typedef struct {
    uint64_t key;
    int32_t  score;
    uint16_t best;     // Move
    uint8_t  depth;
    uint8_t  flag;
} TTEntry;

static TTEntry g_tt[TT_SIZE];

// -------------------------------
// Move ordering: killers + history
// -------------------------------
static Move g_killers[128][2];               // by ply
static uint16_t g_history[64][64];           // from->to (promo ignored for simplicity)

static inline void killers_clear(void) {
    memset(g_killers, 0, sizeof(g_killers));
    memset(g_history, 0, sizeof(g_history));
}

static inline int move_score(const Board *b, Move mv, Move tt_move, int ply) {
    (void)b;
    int s = 0;
    if (mv == tt_move) s += 1000000;

    if (MOVE_ISCAP(mv)) {
        s += 500000; // captures first (if you have MVV-LVA, put it here)
    } else {
        if (mv == g_killers[ply][0]) s += 200000;
        else if (mv == g_killers[ply][1]) s += 180000;
        s += (int)g_history[MOVE_FROM(mv)][MOVE_TO(mv)];
    }

    if (MOVE_PROMO(mv)) s += 250000 + (int)MOVE_PROMO(mv) * 1000;
    return s;
}

static void sort_moves(const Board *b, Move *m, int n, Move tt_move, int ply) {
    // Insertion sort is fine for MAX_MOVES~200 and embedded targets.
    for (int i = 1; i < n; i++) {
        Move key = m[i];
        int key_s = move_score(b, key, tt_move, ply);
        int j = i - 1;
        while (j >= 0) {
            int sj = move_score(b, m[j], tt_move, ply);
            if (sj >= key_s) break;
            m[j+1] = m[j];
            j--;
        }
        m[j+1] = key;
    }
}

// -------------------------------
// Quiescence search (captures/promos)
// -------------------------------
static int quiesce(Board *b, int alpha, int beta, int ply) {
    if (board_is_drawish && board_is_drawish(b)) return 0;

    int stand = eval_position(b);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    MoveList caps;
    gen_captures(b, &caps);

    // order captures (captures already; still prioritize promotions if present)
    sort_moves(b, caps.m, caps.n, 0, ply);

    for (int i = 0; i < caps.n; i++) {
        Move mv = caps.m[i];

        Undo u;
        board_make_move(b, mv, &u);

        int score = -quiesce(b, -beta, -alpha, ply+1);

        board_unmake_move(b, mv, &u);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// -------------------------------
// Negamax + Alpha-Beta + TT
// -------------------------------
static int negamax(Board *b, int depth, int alpha, int beta, int ply) {
    if (board_is_drawish && board_is_drawish(b)) return 0;

    if (depth <= 0) {
        return quiesce(b, alpha, beta, ply);
    }

    uint64_t key = board_hash(b);
    TTEntry *e = &g_tt[key & TT_MASK];

    Move tt_move = 0;
    if (e->key == key) {
        tt_move = e->best;
        if (e->depth >= (uint8_t)depth) {
            int tt_score = e->score;
            if (e->flag == TT_EXACT) return tt_score;
            if (e->flag == TT_LOWER && tt_score > alpha) alpha = tt_score;
            else if (e->flag == TT_UPPER && tt_score < beta) beta = tt_score;
            if (alpha >= beta) return tt_score;
        }
    }

    MoveList moves;
    gen_moves(b, &moves);

    if (moves.n == 0) {
        // No legal moves => mate or stalemate.
        if (board_is_in_check && board_is_in_check(b)) {
            // side to move is mated; ply makes “mate sooner” preferred
            return -MATE + ply;
        }
        return 0;
    }

    sort_moves(b, moves.m, moves.n, tt_move, ply);

    int orig_alpha = alpha;
    Move best = 0;

    for (int i = 0; i < moves.n; i++) {
        Move mv = moves.m[i];

        Undo u;
        board_make_move(b, mv, &u);

        int score = -negamax(b, depth - 1, -beta, -alpha, ply+1);

        board_unmake_move(b, mv, &u);

        if (score > alpha) {
            alpha = score;
            best = mv;

            if (alpha >= beta) {
                // beta cutoff -> update killer/history for quiet moves
                if (!MOVE_ISCAP(mv) && MOVE_PROMO(mv) == 0) {
                    if (g_killers[ply][0] != mv) {
                        g_killers[ply][1] = g_killers[ply][0];
                        g_killers[ply][0] = mv;
                    }
                    uint8_t f = MOVE_FROM(mv), t = MOVE_TO(mv);
                    uint16_t h = g_history[f][t];
                    uint16_t add = (uint16_t)(depth * depth * 8);
                    g_history[f][t] = (uint16_t)(h + add);
                }
                break;
            }
        }
    }

    // Store TT
    uint8_t flag = TT_EXACT;
    if (alpha <= orig_alpha) flag = TT_UPPER;
    else if (alpha >= beta)  flag = TT_LOWER;

    e->key = key;
    e->score = alpha;
    e->best = best ? best : tt_move;
    e->depth = (uint8_t)depth;
    e->flag = flag;

    return alpha;
}

// -------------------------------
// Public API: choose best move depth=6
// -------------------------------
Move choose_best_move(Board *b, int target_depth) {
    // Iterative deepening to target_depth helps move ordering a lot.
    // target_depth = 6 per your request.
    Move best = 0;
    int best_score = -INF;

    for (int d = 1; d <= target_depth; d++) {
        // aspiration window around last score (fast); fallback to full window if fail
        int a = (best ? best_score - 50 : -INF);
        int c = (best ? best_score + 50 :  INF);

        int score = negamax(b, d, a, c, 0);
        if (score <= a || score >= c) {
            score = negamax(b, d, -INF, INF, 0);
        }

        // pull PV move from TT root
        uint64_t key = board_hash(b);
        TTEntry *e = &g_tt[key & TT_MASK];
        if (e->key == key && e->best) {
            best = e->best;
            best_score = score;
        }
    }

    return best;
}

// -------------------------------
// Initialization
// -------------------------------
void engine_init(void) {
    memset(g_tt, 0, sizeof(g_tt));
    killers_clear();
}

/*
 * Notes (important, practical):
 * 1) Depth=6 is realistic on embedded CPU if:
 *    - movegen is fast (your FPGA movegen helps a lot),
 *    - TT is enabled (it is),
 *    - quiescence is kept small (captures only, as written).
 *
 * 2) To push towards depth=10:
 *    - you will need MUCH better pruning + move ordering + incremental make/unmake,
 *      and likely some form of time management + iterative deepening with a cutoff.
 *
 * 3) How to hook your FPGA accelerators:
 *    - gen_moves/gen_captures should call your move-generator hardware (MMIO) to produce
 *      legal moves quickly (ideally generate pseudo-legal then filter legality).
 *    - eval_position should call your DSP evaluator hardware (MMIO) and return score from
 *      side-to-move perspective.
 */