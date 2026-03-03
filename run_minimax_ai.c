// =========================================================
// HARDWARE MEMORY MAP (Dual-Core Coprocessor System)
// =========================================================

// CORE 1: The Move Generator (Calculates Legal Moves)
#define GEN_BASE_ADDR      0x44000000 
#define GEN_REG_PIECE_LOW  *((volatile uint32_t*)(GEN_BASE_ADDR + 0x00))
#define GEN_REG_PIECE_HIGH *((volatile uint32_t*)(GEN_BASE_ADDR + 0x04))
// ... [Mapping Regs 2 through 23 for Friendly/Enemy Pieces] ...
#define GEN_REG_OPCODE     *((volatile uint32_t*)(GEN_BASE_ADDR + 0x60)) // Reg 24
#define GEN_REG_OUT_LOW    *((volatile uint32_t*)(GEN_BASE_ADDR + 0x64)) // Reg 25
#define GEN_REG_OUT_HIGH   *((volatile uint32_t*)(GEN_BASE_ADDR + 0x68)) // Reg 26

// CORE 2: The DSP Evaluator (Calculates Material Advantage)
#define EVAL_BASE_ADDR      0x44010000 
#define EVAL_REG_PAWN_LOW   *((volatile uint32_t*)(EVAL_BASE_ADDR + 0x00))
#define EVAL_REG_PAWN_HIGH  *((volatile uint32_t*)(EVAL_BASE_ADDR + 0x04))
// ... [Mapping Regs 2 through 23 for All Bitboards] ...
#define EVAL_REG_SCORE_OUT  *((volatile int32_t*)(EVAL_BASE_ADDR + 0x64)) // Reg 25 (Signed Int!)

// =========================================================
// HARDWARE DRIVER WRAPPERS
// =========================================================

// Fires the Kogge-Stone Pipeline (Returns exactly 6 cycles later)
uint64_t hw_get_moves(uint64_t* board_state, uint32_t opcode) {
    // 1. Write the full board state to the Move Generator registers
    // (In reality, your friend would loop through his bitboard array here)
    GEN_REG_PIECE_LOW  = (uint32_t)(board_state[opcode] & 0xFFFFFFFF);
    GEN_REG_PIECE_HIGH = (uint32_t)(board_state[opcode] >> 32);
    // ... write the rest of the board ...
    
    // 2. Fire the Opcode
    GEN_REG_OPCODE = opcode; 

    // 3. Read the Result (The hardware is so fast, no polling is needed)
    uint32_t low = GEN_REG_OUT_LOW;
    uint32_t high = GEN_REG_OUT_HIGH;
    return ((uint64_t)high << 32) | low;
}

// Fires the DSP Pipeline (Returns exactly 6 cycles later)
int hw_get_score(uint64_t* board_state) {
    // 1. Write the full board state to the Evaluator registers
    EVAL_REG_PAWN_LOW  = (uint32_t)(board_state[0] & 0xFFFFFFFF);
    EVAL_REG_PAWN_HIGH = (uint32_t)(board_state[0] >> 32);
    // ... write the rest of the board ...

    // 2. Read the DSP multiplied score!
    return EVAL_REG_SCORE_OUT; 
}