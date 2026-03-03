#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "platform.h"
#include "xparameters.h"
#include "xuartlite_l.h"
#include "xil_printf.h"

// =========================================================
// 1. HARDWARE MEMORY MAP (The 10-Register Coprocessor)
// =========================================================
#define UART_BASE_ADDR     XPAR_UARTLITE_0_BASEADDR
#define HW_BASE_ADDR       0x44000000 

// The AXI registers are spaced 4 bytes (32 bits) apart
#define HW_REG_PIECE_LOW   *((volatile uint32_t*)(HW_BASE_ADDR + 0x00))
#define HW_REG_PIECE_HIGH  *((volatile uint32_t*)(HW_BASE_ADDR + 0x04))
#define HW_REG_FRIEND_LOW  *((volatile uint32_t*)(HW_BASE_ADDR + 0x08))
#define HW_REG_FRIEND_HIGH *((volatile uint32_t*)(HW_BASE_ADDR + 0x0C))
#define HW_REG_ENEMY_LOW   *((volatile uint32_t*)(HW_BASE_ADDR + 0x10))
#define HW_REG_ENEMY_HIGH  *((volatile uint32_t*)(HW_BASE_ADDR + 0x14))
#define HW_REG_OPCODE      *((volatile uint32_t*)(HW_BASE_ADDR + 0x18))
#define HW_REG_OUT_LOW     *((volatile uint32_t*)(HW_BASE_ADDR + 0x1C))
#define HW_REG_OUT_HIGH    *((volatile uint32_t*)(HW_BASE_ADDR + 0x20))

// Opcode Definitions
#define OP_KNIGHT     0
#define OP_KING       1
#define OP_W_PAWN     2
#define OP_B_PAWN     3

// =========================================================
// 2. THE UNIFIED HARDWARE WRAPPER (Your Job - DONE)
// =========================================================
uint64_t get_hardware_moves(uint64_t piece_board, uint64_t friendly_board, uint64_t enemy_board, uint32_t opcode) {
    // 1. Load the bitboards into the custom silicon
    HW_REG_PIECE_LOW   = (uint32_t)(piece_board & 0xFFFFFFFF);
    HW_REG_PIECE_HIGH  = (uint32_t)(piece_board >> 32);
    
    HW_REG_FRIEND_LOW  = (uint32_t)(friendly_board & 0xFFFFFFFF);
    HW_REG_FRIEND_HIGH = (uint32_t)(friendly_board >> 32);
    
    HW_REG_ENEMY_LOW   = (uint32_t)(enemy_board & 0xFFFFFFFF);
    HW_REG_ENEMY_HIGH  = (uint32_t)(enemy_board >> 32);

    // 2. Fire the instruction! The hardware calculates instantly.
    HW_REG_OPCODE = opcode;

    // 3. Read the 64-bit result back from the AXI bus
    uint32_t out_low  = HW_REG_OUT_LOW;
    uint32_t out_high = HW_REG_OUT_HIGH;

    return ((uint64_t)out_high << 32) | out_low;
}

// =========================================================
// 3. YOUR FRIEND'S TO-DO LIST (Software AI Engine)
// =========================================================

// TASK 1: State Management
// Your friend must define the 64-bit integers that represent the board 
// and update them whenever a move is made or unmade during the search.
uint64_t white_pawns, white_knights, white_kings; // ... etc
uint64_t black_pawns, black_knights, black_kings; // ... etc

// TASK 2: Software Fallback for Sliding Pieces
// Since the hardware doesn't do Rooks, Bishops, or Queens yet, 
// your friend must write standard C functions to calculate their moves.
uint64_t get_software_rook_moves(uint64_t rook_board, uint64_t all_pieces) {
    // Implement magic bitboards or ray-casting loops here
    return 0; 
}

// TASK 3: The Board Evaluator
int evaluate_board() {
    // Calculate material score (e.g., Queen = 900, Pawn = 100)
    return 0; 
}

// TASK 4: The Minimax Search
int minimax(int depth, bool is_maximizing, int alpha, int beta) {
    if (depth == 0) return evaluate_board();

    // 1. Generate all legal moves. 
    //    He will call get_hardware_moves(..., OP_KNIGHT) for knights, etc.,
    //    and get_software_rook_moves() for the sliding pieces.
    
    // 2. Loop through every legal move.
    
    // 3. Make the move on the bitboards.
    
    // 4. score = minimax(depth - 1, !is_maximizing, alpha, beta);
    
    // 5. Unmake the move on the bitboards.
    
    // 6. Apply Alpha-Beta pruning logic.

    return 0; // Return best score
}

// =========================================================
// 4. THE GAME LOOP (PC Communication - DONE)
// =========================================================
int main() {
    init_platform();
    xil_printf("\r\n--- FPGA Chess Coprocessor Booted ---\r\n");

    char player_move[5]; 
    char ai_response[5];

    while (1) {
        // Wait for 4 bytes from Python GUI over USB
        for (int i = 0; i < 4; i++) {
            player_move[i] = XUartLite_RecvByte(UART_BASE_ADDR);
        }
        player_move[4] = '\0'; 
        
        // --- FRIEND'S LOGIC GOES HERE ---
        // 1. Parse player_move (e.g., "e2e4") and update bitboards.
        // 2. Call minimax() to find the best response.
        // 3. Convert best response to a string and put it in ai_response.
        strcpy(ai_response, "e7e5"); // Placeholder hardcode
        // --------------------------------
        
        // Blast the AI's move back to the Python GUI
        xil_printf("AI_MOVE:%s\r\n", ai_response);
    }

    cleanup_platform();
    return 0;
}