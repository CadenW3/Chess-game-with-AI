#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "xuartlite_l.h"
#include "xparameters.h"

#define UART_BASE_ADDR XPAR_UARTLITE_0_BASEADDR

// =========================================================
// THE HARDWARE ACCELERATOR WRAPPERS (Your Job)
// =========================================================
#define HW_BASE_ADDR 0x44000000 
// (You will add the memory mapping and functions here once 
// you design the Verilog IP).


// =========================================================
// THE AI ENGINE (Your Friend's Job)
// =========================================================
void run_minimax_ai(char* player_move, char* ai_response_out) {
    /* * FRIEND'S INSTRUCTIONS:
     * 1. 'player_move' is a 4-character string (e.g., "e2e4").
     * 2. Parse this string, update your internal C bitboards.
     * 3. Run your Minimax / PVS algorithm to find the best move for Black.
     * 4. Convert your best move back into a 4-character string.
     * 5. Copy that string into 'ai_response_out'.
     */

    // --- YOUR FRIEND'S ALGORITHM GOES HERE ---
    
    
    // For testing the pipeline right now, we hardcode a fake AI response:
    strcpy(ai_response_out, "e7e5"); 
}

// =========================================================
// THE COMMUNICATION BRIDGE (The Server Loop)
// =========================================================
int main() {
    init_platform();
    xil_printf("System Ready\r\n");

    char player_move[5];
    char ai_response[5];

    while (1) {
        // 1. Wait to receive 4 bytes (e.g., "e2e4") from the Python GUI
        for (int i = 0; i < 4; i++) {
            player_move[i] = XUartLite_RecvByte(UART_BASE_ADDR);
        }
        player_move[4] = '\0'; // Null terminate the string
        
        // 2. Pass it to your friend's algorithm
        run_minimax_ai(player_move, ai_response);

        // 3. Send the AI's move back to Python in the exact format it expects
        xil_printf("AI_MOVE:%s\r\n", ai_response);
    }

    cleanup_platform();
    return 0;
}