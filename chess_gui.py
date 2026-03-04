import pygame
import chess
import serial
import threading
import sys
import subprocess
import time

# --- CONFIGURATION ---
PC_MODE = True  
FPGA_COM_PORT = 'COM3' 
BAUD_RATE = 115200 
ENGINE_PATH = './chess_ai.exe' 

# Colors
LIGHT_SQUARE = (238, 238, 210)
DARK_SQUARE = (118, 150, 86)
SELECTED_HIGHLIGHT = (186, 202, 68)    
LAST_MOVE_HIGHLIGHT = (246, 246, 105)  

PIECES = {
    'P': '♙', 'N': '♘', 'B': '♗', 'R': '♖', 'Q': '♕', 'K': '♔',
    'p': '♟', 'n': '♞', 'b': '♝', 'r': '♜', 'q': '♛', 'k': '♚'
}

class ChessGUI:
    def __init__(self):
        pygame.init()
        self.width, self.board_height = 600, 600
        self.ui_height = 50
        self.sq_size = self.width // 8
        self.screen = pygame.display.set_mode((self.width, self.board_height + self.ui_height))
        pygame.display.set_caption("Hardware Accelerated FPGA Chess")
        
        self.font = pygame.font.SysFont("Segoe UI Symbol", self.sq_size - 10)
        self.ui_font = pygame.font.SysFont("Arial", 20, bold=True)
        
        self.board = chess.Board()
        self.selected_sq = None
        self.last_move = None  
        self.ai_thinking = False
        self.game_over = False
        self.live_score = 0
        self.banned_moves = [] # THE VERIFICATION LIST
        
        self.reset_rect = pygame.Rect(480, 610, 100, 30)

        self.fpga_conn = None
        self.pc_engine = None

        if PC_MODE:
            try:
                self.pc_engine = subprocess.Popen(
                    [ENGINE_PATH], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                    universal_newlines=True, bufsize=1
                )
            except Exception as e:
                print(f"FATAL ERROR: Could not launch {ENGINE_PATH}.\n{e}")
                sys.exit(1)
        else:
            try:
                self.fpga_conn = serial.Serial(FPGA_COM_PORT, BAUD_RATE, timeout=0.1)
                time.sleep(1) 
            except Exception as e:
                print(f"WARNING: Could not connect to FPGA.\n{e}")

    def draw_board(self):
        self.screen.fill((240, 240, 240)) 
        
        for row in range(8):
            for col in range(8):
                color = LIGHT_SQUARE if (row + col) % 2 == 0 else DARK_SQUARE
                sq_index = (7 - row) * 8 + col
                
                if self.last_move and sq_index in (self.last_move.from_square, self.last_move.to_square):
                    color = LAST_MOVE_HIGHLIGHT
                
                if self.selected_sq == sq_index:
                    color = SELECTED_HIGHLIGHT
                
                rect = pygame.Rect(col * self.sq_size, row * self.sq_size, self.sq_size, self.sq_size)
                pygame.draw.rect(self.screen, color, rect)

                piece = self.board.piece_at(sq_index)
                if piece:
                    text_surface = self.font.render(PIECES[piece.symbol()], True, (0, 0, 0))
                    text_rect = text_surface.get_rect(center=rect.center)
                    self.screen.blit(text_surface, text_rect)
        
        eval_pawns = (-self.live_score) / 100.0 
        
        if eval_pawns > 0:
            score_str = f"+{eval_pawns:.1f}"
            score_color = (0, 150, 0)
        elif eval_pawns < 0:
            score_str = f"{eval_pawns:.1f}"
            score_color = (200, 0, 0)
        else:
            score_str = "0.0"
            score_color = (100, 100, 100)

        score_text = self.ui_font.render(f"Eval: {score_str}", True, score_color)
        self.screen.blit(score_text, (10, 615))

        if self.board.is_checkmate() or self.game_over:
            winner = "White" if not self.board.turn else "Black"
            status_text = self.ui_font.render(f"CHECKMATE! {winner} Wins.", True, (255, 0, 0))
            self.screen.blit(status_text, (150, 615))
        elif self.board.is_stalemate():
            status_text = self.ui_font.render("STALEMATE! Game is a Draw.", True, (0, 0, 255))
            self.screen.blit(status_text, (150, 615))
        elif self.ai_thinking:
            status_text = self.ui_font.render("AI is Thinking...", True, (100, 100, 100))
            self.screen.blit(status_text, (200, 615))

        pygame.draw.rect(self.screen, (220, 50, 50), self.reset_rect, border_radius=5)
        reset_text = self.ui_font.render("RESET", True, (255, 255, 255))
        self.screen.blit(reset_text, (self.reset_rect.x + 20, self.reset_rect.y + 3))

        pygame.display.flip()

    def listen_to_engine(self):
        while True:
            msg = ""
            if PC_MODE and self.pc_engine:
                msg = self.pc_engine.stdout.readline().strip()
            elif not PC_MODE and self.fpga_conn and self.fpga_conn.in_waiting > 0:
                msg = self.fpga_conn.readline().decode('ascii').strip()

            if msg:
                print(f"[ENGINE]: {msg}")
                if msg.startswith("SCORE:"):
                    try:
                        self.live_score = int(msg.split(":")[1])
                    except ValueError:
                        pass
                elif msg.startswith("AI_MOVE:"):
                    ai_move_str = msg.split(":")[-1].strip()
                    
                    if ai_move_str == "MATE":
                        self.game_over = True
                        self.ai_thinking = False
                    else:
                        try:
                            move = chess.Move.from_uci(ai_move_str)
                            if move in self.board.legal_moves:
                                self.board.push(move)
                                self.last_move = move 
                                if self.board.is_checkmate():
                                    self.game_over = True
                                self.ai_thinking = False
                            else:
                                # --- THE VERIFICATION LOOP ---
                                self.banned_moves.append(ai_move_str)
                                banned_str = ",".join(self.banned_moves)
                                print(f"REFEREE: AI sent illegal move '{ai_move_str}'. Re-requesting with bans: {banned_str}")
                                
                                board_fen = self.board.fen().split()[0]
                                recalc_msg = f"{board_fen} {banned_str}\n"
                                
                                if PC_MODE and self.pc_engine:
                                    self.pc_engine.stdin.write(recalc_msg)
                                    self.pc_engine.stdin.flush()
                                elif not PC_MODE and self.fpga_conn:
                                    self.fpga_conn.write(recalc_msg.encode('ascii'))
                                # Note: self.ai_thinking remains True so the GUI waits for the new move
                        except ValueError:
                            self.ai_thinking = False

    def run(self):
        listener = threading.Thread(target=self.listen_to_engine, daemon=True)
        listener.start()

        running = True
        while running:
            self.draw_board()
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                
                elif event.type == pygame.MOUSEBUTTONDOWN:
                    if self.reset_rect.collidepoint(event.pos):
                        self.board.reset()
                        self.game_over = False
                        self.ai_thinking = False
                        self.selected_sq = None
                        self.last_move = None
                        self.live_score = 0
                        self.banned_moves = []
                        continue

                    if self.ai_thinking or self.board.is_game_over() or self.game_over:
                        continue

                    x, y = pygame.mouse.get_pos()
                    if y > self.board_height: continue 

                    col = x // self.sq_size
                    row = y // self.sq_size
                    clicked_sq = (7 - row) * 8 + col

                    if self.selected_sq is None:
                        if self.board.piece_at(clicked_sq) and self.board.color_at(clicked_sq) == chess.WHITE:
                            self.selected_sq = clicked_sq
                    else:
                        move = chess.Move(self.selected_sq, clicked_sq)
                        
                        if self.board.piece_at(self.selected_sq).piece_type == chess.PAWN and (7 - row) == 7:
                            move = chess.Move(self.selected_sq, clicked_sq, promotion=chess.QUEEN)

                        if move in self.board.legal_moves:
                            self.board.push(move)
                            self.last_move = move 
                            
                            self.selected_sq = None
                            self.draw_board() 
                            
                            if self.board.is_checkmate():
                                self.game_over = True
                            else:
                                self.ai_thinking = True
                                self.banned_moves = [] # Reset bans on a new turn
                                
                                board_fen = self.board.fen().split()[0]
                                msg_to_send = f"{board_fen} none\n"
                                
                                if PC_MODE and self.pc_engine:
                                    self.pc_engine.stdin.write(msg_to_send)
                                    self.pc_engine.stdin.flush()
                                elif not PC_MODE and self.fpga_conn:
                                    self.fpga_conn.write(msg_to_send.encode('ascii'))
                        else:
                            self.selected_sq = None 

        if self.pc_engine:
            self.pc_engine.terminate()
        pygame.quit()
        sys.exit()

if __name__ == "__main__":
    gui = ChessGUI()
    gui.run()