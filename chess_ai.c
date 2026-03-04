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
} Move;

Move best_root_move; 

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
}

int get_score() {
#if PC_MODE == 1
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
#else
    for(int i = 0; i < 12; i++) {
        EVAL_REGS[i*2] = (uint32_t)(bitboards[i] & 0xFFFFFFFF); EVAL_REGS[i*2 + 1] = (uint32_t)(bitboards[i] >> 32);
    }
    return (int)EVAL_REGS[25]; 
#endif
}

uint64_t get_moves(int opcode, uint64_t piece) {
#if PC_MODE == 1
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
#else
    for(int i = 0; i < 12; i++) {
        GEN_REGS[i*2] = (uint32_t)(bitboards[i] & 0xFFFFFFFF); GEN_REGS[i*2 + 1] = (uint32_t)(bitboards[i] >> 32);
    }
    GEN_REGS[24] = opcode; 
    return ((uint64_t)GEN_REGS[26] << 32) | GEN_REGS[25];
#endif
}

void make_move(Move m) {
    bitboards[m.piece_opcode] &= ~(1ULL << m.from_sq);
    if (m.promotion_opcode != -1) bitboards[m.promotion_opcode] |= (1ULL << m.to_sq);
    else bitboards[m.piece_opcode] |= (1ULL << m.to_sq);
    if (m.captured_piece != -1) bitboards[m.captured_piece] &= ~(1ULL << m.to_sq);
}

void unmake_move(Move m) {
    if (m.promotion_opcode != -1) bitboards[m.promotion_opcode] &= ~(1ULL << m.to_sq);
    else bitboards[m.piece_opcode] &= ~(1ULL << m.to_sq);
    bitboards[m.piece_opcode] |= (1ULL << m.from_sq);
    if (m.captured_piece != -1) bitboards[m.captured_piece] |= (1ULL << m.to_sq);
}

// =========================================================
// NEW: HARDWARE CHECK DETECTION
// =========================================================
bool is_in_check(int is_white) {
    int king_op = is_white ? 1 : 7;
    uint64_t king_board = bitboards[king_op];
    if (!king_board) return true; // Dead king counts as check
    int k_sq = __builtin_ctzll(king_board);

    int e_start = is_white ? 6 : 0;
    int e_end   = is_white ? 11 : 5;
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

int generate_all_moves(int is_white, Move* move_list) {
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
                move_list[count++] = (Move){op, from_sq, to_sq, cap_op, prom_op};
                attacks &= attacks - 1;
            }
            active &= active - 1;
        }
    }
    return count;
}

int minimax(int depth, int alpha, int beta, int is_white, bool is_root) {
    int current_score = get_score();
    
    if (depth == 0) return is_white ? current_score : -current_score; 

    Move move_list[256];
    int move_count = generate_all_moves(is_white, move_list);
    int best_eval = -9999999;
    
    if (is_root) { best_root_move.from_sq = 0; best_root_move.to_sq = 0; }
    
    bool found_legal_move = false;

    for (int i = 0; i < move_count; i++) {
        // Python Failsafe ban check
        if (is_root && strcmp(banned_moves, "none") != 0) {
            char current_move_str[10];
            move_to_string(move_list[i], current_move_str);
            if (strstr(banned_moves, current_move_str) != NULL) continue; 
        }

        make_move(move_list[i]);
        
        // --- THE CRITICAL FIX ---
        // If making this move leaves us in check, it is physically illegal. Prune it!
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
    
    // If we have zero legal moves, we are either Checkmated or Stalemated.
    if (!found_legal_move) {
        if (is_in_check(is_white)) return -999999 + (10 - depth); // Checkmate (reward faster mates)
        return 0; // Stalemate
    }
    
    return best_eval;
}

int main() {
    char fen_buffer[100];
    char ai_move_str[10];

    printf("--- PC Simulation Mode Booted ---\n");
    fflush(stdout);

    while(1) {
        if (scanf("%99s %199s", fen_buffer, banned_moves) == EOF) break; 
        
        parse_fen(fen_buffer);

        // --- ADAPTIVE DEPTH LOGIC START ---
        int piece_count = 0;
        // Count every piece currently on the board
        for(int i = 0; i < 12; i++) {
            uint64_t b = bitboards[i];
            while(b) { 
                piece_count++; 
                b &= b - 1; // Brian Kernighan's algorithm to pop bits fast
            }
        }

        int dynamic_depth;
        if (piece_count <= 6) {
            dynamic_depth = 10; // Endgame: Kings + 4 pieces (Deep calculation)
        } else if (piece_count <= 12) {
            dynamic_depth = 8;  // Late Midgame
        } else if (piece_count <= 22) {
            dynamic_depth = 6;  // Standard Play
        } else {
            dynamic_depth = 4;  // Heavy Opening (Keeps the game from freezing)
        }
        // --- ADAPTIVE DEPTH LOGIC END ---

        // Inject the dynamic_depth variable into your minimax call
        int best_score = minimax(dynamic_depth, -999999, 999999, 0, true); 

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