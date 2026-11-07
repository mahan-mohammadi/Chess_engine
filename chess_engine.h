/*
 * chess_engine.h - public interface of the educational chess engine.
 * Usable from both C (the CLI) and C++ (the FLTK GUI).
 *
 * Squares are 0..63 with a1 = 0, b1 = 1, ..., h8 = 63.
 * Pieces: White uppercase, Black lowercase, '.' = empty.
 */
#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MOVES 256
#define INF 1000000
#define MATE 900000

enum { WHITE = 1, BLACK = -1 };
enum { EMPTY = '.', PAWN = 'p', KNIGHT = 'n', BISHOP = 'b',
       ROOK = 'r', QUEEN = 'q', KING = 'k' };

typedef struct {
    int from, to;          /* squares 0..63, a1 = 0 */
    char promotion;        /* q/r/b/n, or 0 */
    int en_passant;
    int castle;
} Move;

typedef struct {
    char board[64];        /* White uppercase, Black lowercase */
    int side;              /* WHITE or BLACK */
    unsigned castling;     /* 1=WK, 2=WQ, 4=BK, 8=BQ */
    int ep_square;         /* -1 if none */
    int halfmove_clock;
    int fullmove_number;
} Position;

typedef struct {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

/* Result of game_status(). */
enum {
    GAME_ONGOING = 0,
    GAME_CHECKMATE,        /* side to move is checkmated */
    GAME_STALEMATE,
    GAME_FIFTY_MOVE,
    GAME_INSUFFICIENT,
    GAME_THREEFOLD,
    GAME_TOO_LONG          /* history buffer nearly full */
};

/* ---- small helpers ---- */
static inline int file_of(int sq) { return sq % 8; }
static inline int rank_of(int sq) { return sq / 8; }
static inline int color_of(char p) {
    if (p == '.') return 0;
    return isupper((unsigned char)p) ? WHITE : BLACK;
}
static inline char lower_piece(char p) { return (char)tolower((unsigned char)p); }
static inline char piece_for(int side, char p) {
    return side == WHITE ? (char)toupper((unsigned char)p) : p;
}
static inline int same_move(Move a, Move b) {
    return a.from == b.from && a.to == b.to &&
           lower_piece(a.promotion ? a.promotion : '.') ==
           lower_piece(b.promotion ? b.promotion : '.');
}

/* ---- engine API ---- */

/* Call once at program start. */
void engine_init(void);

void start_position(Position *p);
int  find_king(const Position *p, int side);       /* square, or -1 */
int  in_check(const Position *p, int side);
void generate_legal(const Position *p, MoveList *legal);
void make_move(Position *p, Move m);

/* Negamax alpha-beta. best_score is from the side-to-move's view, in
 * centipawns. Returns a move with from == -1 if there is no legal move.
 * The search reads and temporarily extends the global position history,
 * so do not call hist_* from another thread while it runs. */
Move find_best_move(Position *p, int depth, int *best_score);

/* ---- repetition history ----
 * After start_position(): hist_reset(); hist_push(&pos);
 * After every make_move() on the real game: hist_push(&pos).
 * When taking a move back: hist_pop(). */
void hist_reset(void);
void hist_push(const Position *p);
void hist_pop(void);

/* GAME_* code for the position at the top of the history. */
int  game_status(const Position *p);
const char *game_status_text(int status, int side_to_move);

/* Coordinate notation ("e2e4", "e7e8q"). out must hold 8 bytes. */
void move_to_str(Move m, char out[8]);

#ifdef __cplusplus
}
#endif

#endif /* CHESS_ENGINE_H */
