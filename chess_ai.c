#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PC_MODE 1

#if PC_MODE == 0
    #include "platform.h"
    #include "xil_printf.h"
    #define GEN_BASE_ADDR   0x44000000 
    #define EVAL_BASE_ADDR  0x44010000 
    volatile uint32_t* GEN_REGS = (volatile uint32_t*)GEN_BASE_ADDR;
    volatile uint32_t* EVAL_REGS = (volatile uint32_t*)EVAL_BASE_ADDR;
#endif

uint64_t bitboards[12]; 
int weights[12] = {300, 20000, 100, 500, 300, 900, 300, 20000, 100, 500, 300, 900};
char banned_moves[200]; 

const int center_bonus[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  5,  5,  5,  5,  5,  5,  0,
    0,  5, 10, 10, 10, 10,  5,  0,
    0,  5, 10, 15, 15, 10,  5,  0,
    0,  5, 10, 15, 15, 10,  5,  0,
    0,  5, 10, 10, 10, 10,  5,  0,
    0,  5,  5,  5,  5,  5,  5,  0,
    0,  0,  0,  0,  0,  0,  0,  0
};

typedef struct {
    int piece_opcode;
    int from_sq;
    int to_sq;
    int captured_piece;
    int promotion_opcode;
    int score; 
} Move;

Move best_root_move; 
Move prev_best_move; // NEW: Tracks the best move from the previous depth iteration

// =========================================================
// TRANSPOSITION TABLE & ZOBRIST HASHING
// =========================================================
#define TT_SIZE 32768
#define TT_MASK 0x7FFF 
#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA 2

typedef struct {
    uint64_t key;
    int depth;
    int flag;
    int score;
} TTEntry;

TTEntry tt[TT_SIZE];
uint64_t zobrist_pieces[12][64];
uint64_t zobrist_side;
uint64_t board_hash = 0;

uint64_t random_uint64() {
    static uint64_t seed = 0x9E3779B97F4A7C15ULL;
    seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
    return seed * 0x2545F4914F6CDD1DULL;
}

void init_zobrist() {
    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < 64; sq++) {
            zobrist_pieces[p][sq] = random_uint64();
        }
    }
    zobrist_side = random_uint64();
    for (int i = 0; i < TT_SIZE; i++) { tt[i].key = 0; tt[i].depth = -1; }
}

void compute_initial_hash() {
    board_hash = 0;
    for (int p = 0; p < 12; p++) {
        uint64_t b = bitboards[p];
        while (b) {
            int sq = __builtin_ctzll(b);
            board_hash ^= zobrist_pieces[p][sq];
            b &= b - 1;
        }
    }
}

// =========================================================
// PARSERS
// =========================================================
void move_to_string(Move m, char* str) {
    str[0] = (m.from_sq % 8) + 'a'; str[1] = (m.from_sq / 8) + '1';
    str[2] = (m.to_sq % 8) + 'a';   str[3] = (m.to_sq / 8) + '1';
    if (m.promotion_opcode != -1) { str[4] = 'q'; str[5] = '\0'; }
    else { str[4] = '\0'; }
}

void parse_fen(char* fen) {
    for(int i = 0; i < 12; i++) bitboards[i] = 0; 
    int rank = 7; int file = 0;
    
    for(int i = 0; fen[i] != '\0'; i++) {
        char c = fen[i];
        if (c == '/') { rank--; file = 0; }
        else if (c >= '1' && c <= '8') { file += (c - '0'); }
        else {
            int sq = rank * 8 + file;
            if (c == 'N') bitboards[0] |= (1ULL << sq); else if (c == 'K') bitboards[1] |= (1ULL << sq);
            else if (c == 'P') bitboards[2] |= (1ULL << sq); else if (c == 'R') bitboards[3] |= (1ULL << sq);
            else if (c == 'B') bitboards[4] |= (1ULL << sq); else if (c == 'Q') bitboards[5] |= (1ULL << sq);
            else if (c == 'n') bitboards[6] |= (1ULL << sq); else if (c == 'k') bitboards[7] |= (1ULL << sq);
            else if (c == 'p') bitboards[8] |= (1ULL << sq); else if (c == 'r') bitboards[9] |= (1ULL << sq);
            else if (c == 'b') bitboards[10] |= (1ULL << sq); else if (c == 'q') bitboards[11] |= (1ULL << sq);
            file++;
        }
    }
    compute_initial_hash();
}

int get_score() {
    int w_score = 0; int b_score = 0;
    for (int i = 0; i < 6; i++) {
        uint64_t b = bitboards[i];
        while(b) { int sq = __builtin_ctzll(b); w_score += weights[i] + center_bonus[sq]; b &= b - 1; }
    }
    for (int i = 6; i < 12; i++) {
        uint64_t b = bitboards[i];
        while(b) { int sq = __builtin_ctzll(b); b_score += weights[i] + center_bonus[sq]; b &= b - 1; }
    }
    return w_score - b_score;
}

uint64_t get_moves(int opcode, uint64_t piece) {
    if (!piece) return 0; 
    int sq = __builtin_ctzll(piece);
    uint64_t NOT_A = 0xFEFEFEFEFEFEFEFEULL; uint64_t NOT_H = 0x7F7F7F7F7F7F7F7FULL;
    
    uint64_t w_pieces = bitboards[0]|bitboards[1]|bitboards[2]|bitboards[3]|bitboards[4]|bitboards[5];
    uint64_t b_pieces = bitboards[6]|bitboards[7]|bitboards[8]|bitboards[9]|bitboards[10]|bitboards[11];
    uint64_t empty = ~(w_pieces | b_pieces); uint64_t occupied = ~empty;
    uint64_t attacks = 0;

    if (opcode == 2) { 
        uint64_t pushes = (piece << 8) & empty;
        if ((piece & 0xFF00ULL) && pushes) pushes |= (piece << 16) & empty;
        return pushes | ((((piece << 7) & NOT_H) | ((piece << 9) & NOT_A)) & b_pieces);
    }
    if (opcode == 8) { 
        uint64_t pushes = (piece >> 8) & empty;
        if ((piece & 0x00FF000000000000ULL) && pushes) pushes |= (piece >> 16) & empty;
        return pushes | ((((piece >> 9) & NOT_H) | ((piece >> 7) & NOT_A)) & w_pieces);
    }
    
    if (opcode == 0 || opcode == 6) { 
        return ((piece << 17) & NOT_A) | ((piece << 10) & 0xFCFCFCFCFCFCFCFCULL) | ((piece >> 6) & 0xFCFCFCFCFCFCFCFCULL) | 
               ((piece >> 15) & NOT_A) | ((piece << 15) & NOT_H) | ((piece << 6) & 0x3F3F3F3F3F3F3F3FULL) | 
               ((piece >> 10) & 0x3F3F3F3F3F3F3F3FULL) | ((piece >> 17) & NOT_H);
    }
    if (opcode == 1 || opcode == 7) { 
        return ((piece << 1) & NOT_A) | ((piece >> 1) & NOT_H) | (piece << 8) | (piece >> 8) | 
               ((piece << 9) & NOT_A) | ((piece >> 9) & NOT_H) | ((piece << 7) & NOT_H) | ((piece >> 7) & NOT_A);
    }
    
    int r = sq / 8; int c = sq % 8;
    if (opcode == 3 || opcode == 9 || opcode == 5 || opcode == 11) { 
        for (int i = r + 1; i < 8; i++) { attacks |= (1ULL << (i*8 + c)); if (occupied & (1ULL << (i*8 + c))) break; }
        for (int i = r - 1; i >= 0; i--) { attacks |= (1ULL << (i*8 + c)); if (occupied & (1ULL << (i*8 + c))) break; }
        for (int i = c + 1; i < 8; i++) { attacks |= (1ULL << (r*8 + i)); if (occupied & (1ULL << (r*8 + i))) break; }
        for (int i = c - 1; i >= 0; i--) { attacks |= (1ULL << (r*8 + i)); if (occupied & (1ULL << (r*8 + i))) break; }
    }
    if (opcode == 4 || opcode == 10 || opcode == 5 || opcode == 11) { 
        for (int i=1; r+i < 8 && c+i < 8; i++) { attacks |= (1ULL << ((r+i)*8 + (c+i))); if (occupied & (1ULL << ((r+i)*8 + (c+i)))) break; }
        for (int i=1; r+i < 8 && c-i >= 0; i++) { attacks |= (1ULL << ((r+i)*8 + (c-i))); if (occupied & (1ULL << ((r+i)*8 + (c-i)))) break; }
        for (int i=1; r-i >= 0 && c+i < 8; i++) { attacks |= (1ULL << ((r-i)*8 + (c+i))); if (occupied & (1ULL << ((r-i)*8 + (c+i)))) break; }
        for (int i=1; r-i >= 0 && c-i >= 0; i++) { attacks |= (1ULL << ((r-i)*8 + (c-i))); if (occupied & (1ULL << ((r-i)*8 + (c-i)))) break; }
    }
    return attacks;
}

void make_move(Move m) {
    bitboards[m.piece_opcode] &= ~(1ULL << m.from_sq);
    board_hash ^= zobrist_pieces[m.piece_opcode][m.from_sq];

    if (m.promotion_opcode != -1) {
        bitboards[m.promotion_opcode] |= (1ULL << m.to_sq);
        board_hash ^= zobrist_pieces[m.promotion_opcode][m.to_sq];
    } else {
        bitboards[m.piece_opcode] |= (1ULL << m.to_sq);
        board_hash ^= zobrist_pieces[m.piece_opcode][m.to_sq];
    }
    if (m.captured_piece != -1) {
        bitboards[m.captured_piece] &= ~(1ULL << m.to_sq);
        board_hash ^= zobrist_pieces[m.captured_piece][m.to_sq];
    }
}

void unmake_move(Move m) {
    make_move(m); 
    if (m.promotion_opcode != -1) bitboards[m.promotion_opcode] &= ~(1ULL << m.to_sq);
    else bitboards[m.piece_opcode] &= ~(1ULL << m.to_sq);
    
    bitboards[m.piece_opcode] |= (1ULL << m.from_sq);
    if (m.captured_piece != -1) bitboards[m.captured_piece] |= (1ULL << m.to_sq);
}

bool is_in_check(int is_white) {
    int king_op = is_white ? 1 : 7;
    uint64_t king_board = bitboards[king_op];
    if (!king_board) return true; 
    int k_sq = __builtin_ctzll(king_board);

    int e_start = is_white ? 6 : 0; int e_end = is_white ? 11 : 5;
    for (int op = e_start; op <= e_end; op++) {
        uint64_t active = bitboards[op];
        while (active) {
            int sq = __builtin_ctzll(active);
            uint64_t attacks = get_moves(op, 1ULL << sq);
            if ((attacks >> k_sq) & 1) return true;
            active &= active - 1;
        }
    }
    return false;
}

void sort_moves(Move* move_list, int count) {
    for (int i = 1; i < count; i++) {
        Move key = move_list[i];
        int j = i - 1;
        while (j >= 0 && move_list[j].score < key.score) {
            move_list[j + 1] = move_list[j];
            j = j - 1;
        }
        move_list[j + 1] = key;
    }
}

// NEW: Added is_root flag so we know when to boost the PV move
int generate_all_moves(int is_white, Move* move_list, bool is_root) {
    int count = 0; int start = is_white ? 0 : 6; int end = is_white ? 5 : 11;
    for (int op = start; op <= end; op++) {
        uint64_t active = bitboards[op];
        while (active) {
            int from_sq = __builtin_ctzll(active);
            uint64_t backup = bitboards[op]; bitboards[op] = 1ULL << from_sq; 
            uint64_t attacks = get_moves(op, 1ULL << from_sq);
            bitboards[op] = backup; 
            for(int f = start; f <= end; f++) attacks &= ~bitboards[f];

            while (attacks) {
                int to_sq = __builtin_ctzll(attacks); int cap_op = -1;
                for(int e = (is_white ? 6 : 0); e <= (is_white ? 11 : 5); e++) {
                    if ((bitboards[e] >> to_sq) & 1) { cap_op = e; break; }
                }
                int prom_op = -1;
                if (op == 2 && to_sq / 8 == 7) prom_op = 5; 
                if (op == 8 && to_sq / 8 == 0) prom_op = 11; 
                
                int move_score = 0;
                if (cap_op != -1) move_score = 10000 + weights[cap_op] - (weights[op] / 10);
                if (prom_op != -1) move_score += 9000;

                // --- THE ITERATIVE DEEPENING BOOST ---
                // If this move is the absolute best move from the previous iteration, 
                // give it a massive score so it gets sorted to the #1 spot.
                if (is_root && op == prev_best_move.piece_opcode && from_sq == prev_best_move.from_sq && to_sq == prev_best_move.to_sq) {
                    move_score += 1000000; 
                }

                move_list[count++] = (Move){op, from_sq, to_sq, cap_op, prom_op, move_score};
                attacks &= attacks - 1;
            }
            active &= active - 1;
        }
    }
    return count;
}

int minimax(int depth, int alpha, int beta, int is_white, bool is_root) {
    int current_score = get_score();
    
    if (current_score <= -15000 || current_score >= 15000) {
        int mate_score = (current_score > 0) ? current_score + depth : current_score - depth;
        return is_white ? mate_score : -mate_score; 
    }
    if (depth == 0) return is_white ? current_score : -current_score; 

    uint64_t current_hash = board_hash;
    if (!is_white) current_hash ^= zobrist_side;
    
    int tt_index = current_hash & TT_MASK; 
    TTEntry* entry = &tt[tt_index];

    if (entry->key == current_hash && entry->depth >= depth && !is_root) {
        if (entry->flag == TT_EXACT) return entry->score;
        if (entry->flag == TT_ALPHA && entry->score <= alpha) return entry->score;
        if (entry->flag == TT_BETA && entry->score >= beta) return entry->score;
    }

    Move move_list[256];
    int move_count = generate_all_moves(is_white, move_list, is_root);
    if (move_count == 0) return -999999; 
    
    sort_moves(move_list, move_count);

    int best_eval = -9999999;
    if (is_root) { best_root_move.from_sq = 0; best_root_move.to_sq = 0; }
    
    bool found_legal_move = false;
    int alphaOrig = alpha; 

    for (int i = 0; i < move_count; i++) {
        if (is_root && strcmp(banned_moves, "none") != 0) {
            char current_move_str[10];
            move_to_string(move_list[i], current_move_str);
            if (strstr(banned_moves, current_move_str) != NULL) continue; 
        }

        make_move(move_list[i]);
        if (is_in_check(is_white)) {
            unmake_move(move_list[i]);
            continue; 
        }

        int eval = -minimax(depth - 1, -beta, -alpha, !is_white, false);
        unmake_move(move_list[i]);

        if (!found_legal_move || eval > best_eval) {
            best_eval = eval;
            found_legal_move = true;
            if (is_root) best_root_move = move_list[i]; 
        }
        if (eval > alpha) alpha = eval;
        if (alpha >= beta) break; 
    }
    
    if (!found_legal_move) {
        if (is_in_check(is_white)) return -999999 + (10 - depth); 
        return 0; 
    }

    entry->key = current_hash;
    entry->score = best_eval;
    entry->depth = depth;
    if (best_eval <= alphaOrig) entry->flag = TT_ALPHA;
    else if (best_eval >= beta) entry->flag = TT_BETA;
    else entry->flag = TT_EXACT;
    
    return best_eval;
}

int main() {
    char fen_buffer[100];
    char ai_move_str[10];

    init_zobrist();

    printf("--- PC Simulation Mode Booted ---\n");
    fflush(stdout);

    while(1) {
        if (scanf("%99s %199s", fen_buffer, banned_moves) == EOF) break; 
        
        parse_fen(fen_buffer);

        int piece_count = 0;
        for(int i = 0; i < 12; i++) {
            uint64_t b = bitboards[i];
            while(b) { piece_count++; b &= b - 1; }
        }

        int dynamic_depth;
        // Bumped depth to 8 for the opening as requested
        if (piece_count <= 6) dynamic_depth = 12;
        else if (piece_count <= 12) dynamic_depth = 10;
        else if (piece_count <= 26) dynamic_depth = 8;
        else dynamic_depth = 6;

        int best_score = 0;
        
        // Reset the previous best move before starting the new iterative loop
        prev_best_move.piece_opcode = -1;
        prev_best_move.from_sq = 0;
        prev_best_move.to_sq = 0;

        printf("DEBUG: Starting Iterative Deepening to Depth %d...\n", dynamic_depth);
        fflush(stdout);

        // --- THE ITERATIVE DEEPENING LOOP ---
        for (int d = 1; d <= dynamic_depth; d++) {
            best_root_move.from_sq = 0; best_root_move.to_sq = 0; // Clear it for the new depth run
            
            best_score = minimax(d, -999999, 999999, 0, true); 
            
            // Save the best move found at this depth to use as priority in the next loop
            if (best_root_move.from_sq != best_root_move.to_sq) {
                prev_best_move = best_root_move;
            }

            printf("DEBUG: \tFinished Depth %d | Score: %d\n", d, best_score);
            fflush(stdout);

            // If we found a forced mate, there is no need to keep digging. Break out early.
            if (best_score >= 15000 || best_score <= -15000) break;
        }

        // Ensure the absolute best move found across all iterations is the one we play
        best_root_move = prev_best_move;

        printf("SCORE:%d\n", best_score);
        
        if (best_root_move.from_sq == best_root_move.to_sq) {
            printf("AI_MOVE:MATE\n");
        } else {
            make_move(best_root_move);
            move_to_string(best_root_move, ai_move_str);
            printf("AI_MOVE:%s\n", ai_move_str);
        }
        fflush(stdout);
    }
    return 0;
}