/*
 * chess_cli.c - console front end for the chess engine.
 *
 * Input moves in coordinate notation, e.g. e2e4, g1f3, e7e8q.
 * Commands: 'go' (engine moves), 'depth N', 'quit'.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess_engine.h"

static void print_board(const Position *p) {
    puts("\n  +-----------------+");
    for (int r = 7; r >= 0; --r) {
        printf("%d |", r + 1);
        for (int f = 0; f < 8; ++f)
            printf(" %c", p->board[r*8+f]);
        puts(" |");
    }
    puts("  +-----------------+\n    a b c d e f g h\n");
    printf("%s to move\n", p->side == WHITE ? "White" : "Black");
}

static int parse_square(const char *s) {
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8')
        return -1;
    return (s[1]-'1')*8 + (s[0]-'a');
}

static int parse_move(const char *text, Move *out) {
    if (strlen(text) < 4) return 0;
    int from=parse_square(text), to=parse_square(text+2);
    if (from < 0 || to < 0) return 0;
    char promo = 0;
    if (text[4]) {
        promo = (char)tolower((unsigned char)text[4]);
        if (promo!='q' && promo!='r' && promo!='b' && promo!='n') return 0;
    }
    *out=(Move){from,to,promo,0,0};
    return 1;
}

static void print_move(Move m) {
    char buf[8];
    move_to_str(m, buf);
    printf("%s", buf);
}

int main(void) {
    Position position;
    engine_init();
    start_position(&position);
    hist_reset();
    hist_push(&position);

    puts("C Chess Engine - enter moves like e2e4. Type 'go' for engine move,");
    puts("'depth N' to set search depth, 'quit' to exit.");
    int depth = 4;
    char input[128];

    for (;;) {
        print_board(&position);
        MoveList legal;
        generate_legal(&position, &legal);

        int status = game_status(&position);
        if (status != GAME_ONGOING) {
            puts(game_status_text(status, position.side));
            break;
        }

        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\r\n")] = '\0';

        if (strcmp(input, "quit") == 0) break;
        if (strncmp(input, "depth ", 6) == 0) {
            int d = atoi(input + 6);
            if (d >= 1 && d <= 8) depth = d;
            else puts("Choose a depth from 1 to 8.");
            continue;
        }

        if (strcmp(input, "go") == 0) {
            int score = 0;
            Move best = find_best_move(&position, depth, &score);
            if (best.from < 0) { puts("No legal move."); break; }
            printf("Engine plays: ");
            print_move(best);
            printf("  (score: %d centipawns)\n", score);
            make_move(&position, best);
            hist_push(&position);
            continue;
        }

        Move requested;
        if (!parse_move(input, &requested)) {
            puts("Invalid format. Example: e2e4 or e7e8q.");
            continue;
        }

        int found = 0;
        for (int i = 0; i < legal.count; ++i) {
            if (same_move(requested, legal.moves[i])) {
                make_move(&position, legal.moves[i]);
                hist_push(&position);
                found = 1;
                break;
            }
        }
        if (!found) puts("Illegal move. Try another move.");
    }
    return 0;
}
