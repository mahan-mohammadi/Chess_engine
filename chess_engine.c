/*
 * chess_engine.c - minimal educational chess engine.
 *
 *   - 8x8 board, legal move generation (castling, en passant, promotion)
 *   - Check/checkmate/stalemate detection
 *   - Draw detection: repetition (threefold in play, twofold in search),
 *     fifty-move rule, insufficient material
 *   - Material evaluation, negamax alpha-beta search
 *
 * This is a learning engine, not a tournament-strength engine.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess_engine.h"

static const int knight_offsets[8][2] = {
    {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}
};
static const int king_offsets[8][2] = {
    {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
};
static const int bishop_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
static const int rook_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

static int inside(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
static void add_move(MoveList *list, int from, int to, char promo,
                     int ep, int castle) {
    if (list->count >= MAX_MOVES) return;
    list->moves[list->count++] = (Move){from, to, promo, ep, castle};
}

void start_position(Position *p) {
    const char *rows[8] = {
        "rnbqkbnr", "pppppppp", "........", "........",
        "........", "........", "PPPPPPPP", "RNBQKBNR"
    };
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f)
            p->board[(7-r)*8+f] = rows[r][f];
    p->side = WHITE;
    p->castling = 15;
    p->ep_square = -1;
    p->halfmove_clock = 0;
    p->fullmove_number = 1;
}

int find_king(const Position *p, int side) {
    char king = piece_for(side, KING);
    for (int i = 0; i < 64; ++i)
        if (p->board[i] == king) return i;
    return -1;
}

/* Is square sq attacked by the given side? */
static int is_attacked(const Position *p, int sq, int by_side) {
    int f = file_of(sq), r = rank_of(sq);

    /* Pawn attacks: white pawns move toward increasing ranks. */
    int pawn_source_rank = r - by_side;
    for (int df = -1; df <= 1; df += 2) {
        int sf = f - df;
        if (inside(sf, pawn_source_rank)) {
            char x = p->board[pawn_source_rank*8+sf];
            if (x == piece_for(by_side, PAWN)) return 1;
        }
    }

    for (int i = 0; i < 8; ++i) {
        int nf = f + knight_offsets[i][0], nr = r + knight_offsets[i][1];
        if (inside(nf,nr) &&
            p->board[nr*8+nf] == piece_for(by_side, KNIGHT)) return 1;
    }

    for (int i = 0; i < 8; ++i) {
        int nf = f + king_offsets[i][0], nr = r + king_offsets[i][1];
        if (inside(nf,nr) &&
            p->board[nr*8+nf] == piece_for(by_side, KING)) return 1;
    }

    for (int d = 0; d < 8; ++d) {
        int df = d < 4 ? bishop_dirs[d][0] : rook_dirs[d-4][0];
        int dr = d < 4 ? bishop_dirs[d][1] : rook_dirs[d-4][1];
        int nf = f + df, nr = r + dr;
        while (inside(nf,nr)) {
            char x = p->board[nr*8+nf];
            if (x != '.') {
                if (color_of(x) == by_side) {
                    char t = lower_piece(x);
                    if (t == QUEEN ||
                        (d < 4 && t == BISHOP) ||
                        (d >= 4 && t == ROOK))
                        return 1;
                }
                break;
            }
            nf += df; nr += dr;
        }
    }
    return 0;
}

int in_check(const Position *p, int side) {
    int k = find_king(p, side);
    return k >= 0 && is_attacked(p, k, -side);
}

static void add_pawn_move(MoveList *list, int from, int to, int side,
                          int ep, int castle) {
    int last_rank = side == WHITE ? 7 : 0;
    if (rank_of(to) == last_rank) {
        add_move(list, from, to, 'q', ep, castle);
        add_move(list, from, to, 'r', ep, castle);
        add_move(list, from, to, 'b', ep, castle);
        add_move(list, from, to, 'n', ep, castle);
    } else {
        add_move(list, from, to, 0, ep, castle);
    }
}

/* Generate pseudo-legal moves. King safety is checked in generate_legal(). */
static void generate_pseudo(const Position *p, MoveList *list) {
    list->count = 0;
    int side = p->side;

    for (int from = 0; from < 64; ++from) {
        char piece = p->board[from];
        if (color_of(piece) != side) continue;
        char type = lower_piece(piece);
        int f = file_of(from), r = rank_of(from);

        if (type == PAWN) {
            int step = side == WHITE ? 1 : -1;
            int nr = r + step;
            if (inside(f,nr) && p->board[nr*8+f] == '.') {
                add_pawn_move(list, from, nr*8+f, side, 0, 0);
                int start_rank = side == WHITE ? 1 : 6;
                int nr2 = r + 2*step;
                if (r == start_rank && p->board[nr2*8+f] == '.')
                    add_move(list, from, nr2*8+f, 0, 0, 0);
            }
            for (int df = -1; df <= 1; df += 2) {
                int nf = f + df;
                if (!inside(nf,nr)) continue;
                int to = nr*8+nf;
                if (color_of(p->board[to]) == -side)
                    add_pawn_move(list, from, to, side, 0, 0);
                else if (to == p->ep_square)
                    add_move(list, from, to, 0, 1, 0);
            }
        } else if (type == KNIGHT) {
            for (int i = 0; i < 8; ++i) {
                int nf=f+knight_offsets[i][0], nr=r+knight_offsets[i][1];
                if (!inside(nf,nr)) continue;
                int to=nr*8+nf;
                if (color_of(p->board[to]) != side)
                    add_move(list,from,to,0,0,0);
            }
        } else if (type == KING) {
            for (int i = 0; i < 8; ++i) {
                int nf=f+king_offsets[i][0], nr=r+king_offsets[i][1];
                if (!inside(nf,nr)) continue;
                int to=nr*8+nf;
                if (color_of(p->board[to]) != side)
                    add_move(list,from,to,0,0,0);
            }

            /* Castling: king may not be in check or cross an attacked square. */
            if (side == WHITE && from == 4 && !in_check(p, WHITE)) {
                if ((p->castling & 1) && p->board[5]=='.' &&
                    p->board[6]=='.' && p->board[7]=='R' &&
                    !is_attacked(p,5,BLACK) && !is_attacked(p,6,BLACK))
                    add_move(list,4,6,0,0,1);
                if ((p->castling & 2) && p->board[1]=='.' &&
                    p->board[2]=='.' && p->board[3]=='.' &&
                    p->board[0]=='R' && !is_attacked(p,3,BLACK) &&
                    !is_attacked(p,2,BLACK))
                    add_move(list,4,2,0,0,1);
            } else if (side == BLACK && from == 60 && !in_check(p, BLACK)) {
                if ((p->castling & 4) && p->board[61]=='.' &&
                    p->board[62]=='.' && p->board[63]=='r' &&
                    !is_attacked(p,61,WHITE) && !is_attacked(p,62,WHITE))
                    add_move(list,60,62,0,0,1);
                if ((p->castling & 8) && p->board[57]=='.' &&
                    p->board[58]=='.' && p->board[59]=='.' &&
                    p->board[56]=='r' && !is_attacked(p,59,WHITE) &&
                    !is_attacked(p,58,WHITE))
                    add_move(list,60,58,0,0,1);
            }
        } else {
            const int (*dirs)[2] = type == BISHOP ? bishop_dirs : rook_dirs;
            int n_dirs = type == QUEEN ? 8 : 4;
            for (int d = 0; d < n_dirs; ++d) {
                int df, dr;
                if (type == QUEEN) {
                    if (d < 4) { df=bishop_dirs[d][0]; dr=bishop_dirs[d][1]; }
                    else { df=rook_dirs[d-4][0]; dr=rook_dirs[d-4][1]; }
                } else { df=dirs[d][0]; dr=dirs[d][1]; }
                int nf=f+df, nr=r+dr;
                while (inside(nf,nr)) {
                    int to=nr*8+nf;
                    if (color_of(p->board[to]) == side) break;
                    add_move(list,from,to,0,0,0);
                    if (p->board[to] != '.') break;
                    nf+=df; nr+=dr;
                }
            }
        }
    }
}

void make_move(Position *p, Move m) {
    char moving = p->board[m.from];
    char captured = p->board[m.to];
    p->board[m.from] = '.';

    if (m.en_passant)
        p->board[m.to + (p->side == WHITE ? -8 : 8)] = '.';

    p->board[m.to] = m.promotion ? piece_for(p->side, m.promotion) : moving;

    if (m.castle) {
        if (m.to == 6) { p->board[5]='R'; p->board[7]='.'; }
        else if (m.to == 2) { p->board[3]='R'; p->board[0]='.'; }
        else if (m.to == 62) { p->board[61]='r'; p->board[63]='.'; }
        else if (m.to == 58) { p->board[59]='r'; p->board[56]='.'; }
    }

    /* Moving king or rook removes corresponding castling rights. */
    if (moving == 'K') p->castling &= ~3u;
    if (moving == 'k') p->castling &= ~12u;
    if (moving == 'R') {
        if (m.from == 7) p->castling &= ~1u;
        if (m.from == 0) p->castling &= ~2u;
    }
    if (moving == 'r') {
        if (m.from == 63) p->castling &= ~4u;
        if (m.from == 56) p->castling &= ~8u;
    }
    if (captured == 'R') {
        if (m.to == 7) p->castling &= ~1u;
        if (m.to == 0) p->castling &= ~2u;
    }
    if (captured == 'r') {
        if (m.to == 63) p->castling &= ~4u;
        if (m.to == 56) p->castling &= ~8u;
    }

    p->ep_square = -1;
    if (lower_piece(moving) == PAWN && abs(m.to-m.from) == 16)
        p->ep_square = (m.from+m.to)/2;

    if (lower_piece(moving) == PAWN || captured != '.' || m.en_passant)
        p->halfmove_clock = 0;
    else
        ++p->halfmove_clock;

    if (p->side == BLACK) ++p->fullmove_number;
    p->side = -p->side;
}

/* ---------- Position hashing and game history (draw detection) ---------- */

#define MAX_HIST 4096

static uint64_t zob_piece[12][64];
static uint64_t zob_side;
static uint64_t zob_castle[16];
static uint64_t zob_ep[8];

/* History of position hashes along the current game + search path.
 * The top entry (g_hist[g_hist_len-1]) is always the position being searched. */
static uint64_t g_hist[MAX_HIST];
static int g_hist_len = 0;

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void zobrist_init(void) {
    uint64_t state = 0x1234567890ABCDEFULL;   /* fixed seed: reproducible */
    for (int i = 0; i < 12; ++i)
        for (int sq = 0; sq < 64; ++sq)
            zob_piece[i][sq] = splitmix64(&state);
    zob_side = splitmix64(&state);
    for (int i = 0; i < 16; ++i) zob_castle[i] = splitmix64(&state);
    for (int i = 0; i < 8; ++i) zob_ep[i] = splitmix64(&state);
}

static int piece_index(char c) {
    const char *order = "PNBRQKpnbrqk";
    const char *pos = strchr(order, c);
    return pos ? (int)(pos - order) : -1;
}

static uint64_t hash_position(const Position *p) {
    uint64_t h = 0;
    for (int i = 0; i < 64; ++i) {
        if (p->board[i] == '.') continue;
        h ^= zob_piece[piece_index(p->board[i])][i];
    }
    if (p->side == BLACK) h ^= zob_side;
    h ^= zob_castle[p->castling & 15];
    if (p->ep_square >= 0) h ^= zob_ep[file_of(p->ep_square)];
    return h;
}

void hist_push(const Position *p) {
    if (g_hist_len < MAX_HIST) g_hist[g_hist_len++] = hash_position(p);
}

/* How many times has the current position (top of history) occurred,
 * including now? Only positions since the last pawn move or capture can
 * match, and only those with the same side to move (every 2nd entry). */
static int repetition_count(const Position *p) {
    if (g_hist_len == 0) return 1;
    int cur = g_hist_len - 1;
    uint64_t h = g_hist[cur];
    int limit = cur - p->halfmove_clock;
    if (limit < 0) limit = 0;
    int count = 1;
    for (int i = cur - 2; i >= limit; i -= 2)
        if (g_hist[i] == h) ++count;
    return count;
}

/* K vs K, K+minor vs K. */
static int insufficient_material(const Position *p) {
    int minors = 0;
    for (int i = 0; i < 64; ++i) {
        char t = lower_piece(p->board[i]);
        if (t == PAWN || t == ROOK || t == QUEEN) return 0;
        if (t == KNIGHT || t == BISHOP) ++minors;
    }
    return minors <= 1;
}

void generate_legal(const Position *p, MoveList *legal) {
    MoveList pseudo;
    generate_pseudo(p, &pseudo);
    legal->count = 0;
    for (int i = 0; i < pseudo.count; ++i) {
        Position next = *p;
        make_move(&next, pseudo.moves[i]);
        if (!in_check(&next, p->side))
            legal->moves[legal->count++] = pseudo.moves[i];
    }
}

static int piece_value(char p) {
    switch (lower_piece(p)) {
        case PAWN: return 100;
        case KNIGHT: return 320;
        case BISHOP: return 330;
        case ROOK: return 500;
        case QUEEN: return 900;
        default: return 0;
    }
}

/* Score is from the perspective of the side to move. */
static int evaluate(const Position *p) {
    int score = 0;
    for (int i = 0; i < 64; ++i) {
        char x = p->board[i];
        if (x == '.') continue;
        int value = piece_value(x);
        score += color_of(x) == p->side ? value : -value;
    }
    return score;
}

static int move_score(const Position *p, Move m) {
    int score = 0;
    char victim = p->board[m.to];
    char attacker = p->board[m.from];
    if (m.en_passant) victim = piece_for(-p->side, PAWN);
    if (victim != '.') score += 10 * piece_value(victim) - piece_value(attacker);
    if (m.promotion) score += piece_value(m.promotion) + 800;
    return score;
}

static int negamax(Position *p, int depth, int alpha, int beta, int ply) {
    MoveList moves;
    generate_legal(p, &moves);

    if (moves.count == 0)
        return in_check(p, p->side) ? -MATE + ply : 0;

    /* Draws (checked after mate, since mate takes precedence). A single
     * repetition inside the search is scored as a draw: if the position
     * recurred once, either side can steer into the loop again. */
    if (p->halfmove_clock >= 100 || insufficient_material(p) ||
        repetition_count(p) >= 10)
        return 0;

    if (depth == 0)
        return evaluate(p);

    /* Simple move ordering: captures and promotions first. */
    for (int i = 0; i < moves.count; ++i) {
        for (int j = i+1; j < moves.count; ++j) {
            if (move_score(p,moves.moves[j]) > move_score(p,moves.moves[i])) {
                Move tmp=moves.moves[i]; moves.moves[i]=moves.moves[j]; moves.moves[j]=tmp;
            }
        }
    }

    int best = -INF;
    for (int i = 0; i < moves.count; ++i) {
        Position child = *p;
        make_move(&child, moves.moves[i]);
        hist_push(&child);
        int score = -negamax(&child, depth-1, -beta, -alpha, ply+1);
        --g_hist_len;
        if (score > best) best = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }
    return best;
}

Move find_best_move(Position *p, int depth, int *best_score) {
    MoveList moves;
    generate_legal(p, &moves);
    Move best = { -1, -1, 0, 0, 0 };
    int alpha = -INF, beta = INF;
    *best_score = -INF;

    for (int i = 0; i < moves.count; ++i) {
        Position child = *p;
        make_move(&child, moves.moves[i]);
        hist_push(&child);
        int score = -negamax(&child, depth-1, -beta, -alpha, 1);
        --g_hist_len;
        if (score > *best_score) {
            *best_score = score;
            best = moves.moves[i];
        }
        if (score > alpha) alpha = score;
    }
    return best;
}

/* ---------- Public API additions ---------- */

void engine_init(void) { zobrist_init(); }

void hist_reset(void) { g_hist_len = 0; }

void hist_pop(void) { if (g_hist_len > 0) --g_hist_len; }

int game_status(const Position *p) {
    MoveList legal;
    generate_legal(p, &legal);
    if (legal.count == 0)
        return in_check(p, p->side) ? GAME_CHECKMATE : GAME_STALEMATE;
    if (p->halfmove_clock >= 100) return GAME_FIFTY_MOVE;
    if (insufficient_material(p)) return GAME_INSUFFICIENT;
    if (repetition_count(p) >= 3) return GAME_THREEFOLD;
    if (g_hist_len >= MAX_HIST - 64) return GAME_TOO_LONG;
    return GAME_ONGOING;
}

const char *game_status_text(int status, int side_to_move) {
    switch (status) {
        case GAME_CHECKMATE:
            return side_to_move == WHITE ? "Checkmate - Black wins"
                                         : "Checkmate - White wins";
        case GAME_STALEMATE:    return "Draw by stalemate";
        case GAME_FIFTY_MOVE:   return "Draw by the fifty-move rule";
        case GAME_INSUFFICIENT: return "Draw by insufficient material";
        case GAME_THREEFOLD:    return "Draw by threefold repetition";
        case GAME_TOO_LONG:     return "Draw: game too long";
        default:                return "";
    }
}

void move_to_str(Move m, char out[8]) {
    out[0] = (char)('a' + file_of(m.from));
    out[1] = (char)('1' + rank_of(m.from));
    out[2] = (char)('a' + file_of(m.to));
    out[3] = (char)('1' + rank_of(m.to));
    if (m.promotion) { out[4] = m.promotion; out[5] = '\0'; }
    else out[4] = '\0';
}
