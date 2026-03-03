import pygame
import chess
import serial
import threading
import sys

# --- CONFIGURATION ---
FPGA_COM_PORT = 'COM3' # Change to your Nexys A7 COM port
BAUD_RATE = 9600

# Colors for the chess.com look
LIGHT_SQUARE = (238, 238, 210)
DARK_SQUARE = (118, 150, 86)
HIGHLIGHT = (186, 202, 68)

# Unicode Chess Pieces mapping
PIECES = {
    'P': '♙', 'N': '♘', 'B': '♗', 'R': '♖', 'Q': '♕', 'K': '♔',
    'p': '♟', 'n': '♞', 'b': '♝', 'r': '♜', 'q': '♛', 'k': '♚'
}

class ChessGUI:
    def __init__(self):
        pygame.init()
        self.width, self.height = 600, 600
        self.sq_size = self.width // 8
        self.screen = pygame.display.set_mode((self.width, self.height))
        pygame.display.set_caption("Hardware Accelerated FPGA Chess")
        self.font = pygame.font.SysFont("Segoe UI Symbol", self.sq_size - 10)
        
        self.board = chess.Board()
        self.selected_sq = None
        self.fpga_conn = None
        self.ai_thinking = False

        # Attempt to connect to FPGA
        try:
            self.fpga_conn = serial.Serial(FPGA_COM_PORT, BAUD_RATE, timeout=0.1)
            print(f"Connected to FPGA on {FPGA_COM_PORT}")
        except Exception as e:
            print(f"WARNING: Could not connect to FPGA. Running in offline/debug mode.\n{e}")

    def draw_board(self):
        self.screen.fill(pygame.Color("white"))
        for row in range(8):
            for col in range(8):
                # Calculate coordinates (A1 is bottom left)
                color = LIGHT_SQUARE if (row + col) % 2 == 0 else DARK_SQUARE
                rect = pygame.Rect(col * self.sq_size, row * self.sq_size, self.sq_size, self.sq_size)
                
                # Highlight selected square
                sq_index = (7 - row) * 8 + col
                if self.selected_sq == sq_index:
                    color = HIGHLIGHT
                
                pygame.draw.rect(self.screen, color, rect)

                # Draw pieces
                piece = self.board.piece_at(sq_index)
                if piece:
                    text_surface = self.font.render(PIECES[piece.symbol()], True, (0, 0, 0))
                    text_rect = text_surface.get_rect(center=rect.center)
                    self.screen.blit(text_surface, text_rect)
        pygame.display.flip()

    def listen_to_fpga(self):
        """Runs in a background thread to catch the AI's response without freezing the UI."""
        while True:
            if self.fpga_conn and self.fpga_conn.in_waiting > 0:
                msg = self.fpga_conn.readline().decode('ascii').strip()
                print(f"[FPGA]: {msg}")
                if msg.startswith("AI_MOVE:"):
                    ai_move_str = msg.split(":")[1][:4] # Extract e.g., "e7e5"
                    move = chess.Move.from_uci(ai_move_str)
                    if move in self.board.legal_moves:
                        self.board.push(move)
                        self.ai_thinking = False

    def run(self):
        # Start the serial listener thread
        listener = threading.Thread(target=self.listen_to_fpga, daemon=True)
        listener.start()

        running = True
        while running:
            self.draw_board()
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                
                elif event.type == pygame.MOUSEBUTTONDOWN and not self.ai_thinking:
                    # Map mouse click to chess square
                    x, y = pygame.mouse.get_pos()
                    col = x // self.sq_size
                    row = y // self.sq_size
                    clicked_sq = (7 - row) * 8 + col

                    if self.selected_sq is None:
                        # Select piece
                        if self.board.piece_at(clicked_sq) and self.board.color_at(clicked_sq) == chess.WHITE:
                            self.selected_sq = clicked_sq
                    else:
                        # Attempt to move
                        move = chess.Move(self.selected_sq, clicked_sq)
                        if move in self.board.legal_moves:
                            self.board.push(move)
                            move_str = move.uci() # e.g., "e2e4"
                            
                            # Send to FPGA
                            if self.fpga_conn:
                                self.fpga_conn.write(move_str.encode('ascii'))
                                self.ai_thinking = True
                        
                        self.selected_sq = None # Deselect

        pygame.quit()
        sys.exit()

if __name__ == "__main__":
    gui = ChessGUI()
    gui.run()