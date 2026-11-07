/*
 * chess_gui.cpp - FLTK front end for the educational chess engine.
 *
 *   - Click a piece, then a highlighted square, to move
 *   - Promotion popup, last-move / check highlighting
 *   - Play as White, Black, or two-player on one screen
 *   - Engine search runs on a worker thread (the window never freezes)
 *   - Undo, flip board, depth slider, move list
 *
 * Build (see Makefile):  make gui
 */

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Hor_Value_Slider.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_draw.H>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "chess_engine.h"

namespace {

const int SQ        = 70;          /* square size in pixels */
const int BOARD_PX  = SQ * 8;
const int PANEL_W   = 250;
const int STATUS_H  = 30;

enum { MODE_WHITE = 0, MODE_BLACK = 1, MODE_TWO = 2, MODE_AI = 3 };

/* ---------- application state (main thread only) ---------- */

class BoardView;

BoardView         *board_view   = nullptr;
Fl_Choice         *mode_choice  = nullptr;
Fl_Hor_Value_Slider *depth_white = nullptr;   /* search depth when the engine plays White */
Fl_Hor_Value_Slider *depth_black = nullptr;   /* ... and when it plays Black */
Fl_Button         *pause_btn    = nullptr;
Fl_Button         *new_btn      = nullptr;
Fl_Button         *undo_btn     = nullptr;
Fl_Button         *flip_btn     = nullptr;
Fl_Check_Button   *letters_btn  = nullptr;
Fl_Text_Display   *move_view    = nullptr;
Fl_Text_Buffer    *move_buf     = nullptr;
Fl_Box            *status_box   = nullptr;

Position           pos;
MoveList           legal;
std::vector<Position> prev_positions;   /* position before each ply */
std::vector<Move>     played;           /* moves made so far */
int                selected    = -1;
int                last_from   = -1;
int                last_to     = -1;
int                status      = GAME_ONGOING;
bool               flipped     = false;
bool               thinking    = false;
bool               letter_mode = false;
bool               paused      = false;     /* engine-vs-engine only */
std::string        eval_text;

void on_square_clicked(int sq);
void update_ui();

/* ---------- helpers ---------- */

int current_mode() { return mode_choice ? mode_choice->value() : MODE_WHITE; }

bool is_human_turn() {
    const int mode = current_mode();
    if (mode == MODE_AI) return false;          /* engine plays both sides */
    if (mode == MODE_TWO) return true;
    return pos.side == (mode == MODE_WHITE ? WHITE : BLACK);
}

bool has_moves_from(int sq) {
    for (int i = 0; i < legal.count; ++i)
        if (legal.moves[i].from == sq) return true;
    return false;
}

bool can_undo() {
    if (thinking || played.empty()) return false;
    if (current_mode() == MODE_BLACK) return played.size() >= 2;
    return true;
}

char ask_promotion() {
    static Fl_Menu_Item items[] = {
        { "Queen",  0, 0, (void *)(intptr_t)'q' },
        { "Rook",   0, 0, (void *)(intptr_t)'r' },
        { "Bishop", 0, 0, (void *)(intptr_t)'b' },
        { "Knight", 0, 0, (void *)(intptr_t)'n' },
        { 0 }
    };
    const Fl_Menu_Item *m = items->popup(Fl::event_x(), Fl::event_y(), "Promote to");
    return m ? (char)(intptr_t)m->user_data() : 0;
}

/* ---------- board widget ---------- */

class BoardView : public Fl_Widget {
public:
    BoardView(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}

    void draw() override {
        const int king_sq = in_check(&pos, pos.side) ? find_king(&pos, pos.side) : -1;

        for (int sq = 0; sq < 64; ++sq) {
            const int f = file_of(sq), r = rank_of(sq);
            const int px = x() + (flipped ? 7 - f : f) * SQ;
            const int py = y() + (flipped ? r : 7 - r) * SQ;
            const bool light = ((f + r) % 2) == 1;      /* a1 is dark */

            /* square colour */
            if (sq == king_sq)
                fl_color(222, 92, 80);
            else if (sq == selected)
                fl_color(130, 151, 105);
            else if (sq == last_from || sq == last_to)
                light ? fl_color(205, 210, 106) : fl_color(170, 162, 58);
            else
                light ? fl_color(240, 217, 181) : fl_color(181, 136, 99);
            fl_rectf(px, py, SQ, SQ);

            /* legal-move markers for the selected piece */
            if (selected >= 0) {
                for (int i = 0; i < legal.count; ++i) {
                    const Move &m = legal.moves[i];
                    if (m.from != selected || m.to != sq) continue;
                    fl_color(90, 110, 60);
                    if (pos.board[sq] == '.' && !m.en_passant) {
                        const int d = SQ / 5;
                        fl_pie(px + SQ / 2 - d / 2, py + SQ / 2 - d / 2, d, d, 0, 360);
                    } else {
                        fl_line_style(FL_SOLID, 4);
                        fl_arc(px + 4, py + 4, SQ - 8, SQ - 8, 0, 360);
                        fl_line_style(0);
                    }
                    break;
                }
            }

            /* piece */
            if (pos.board[sq] != '.') draw_piece(px, py, pos.board[sq]);

            /* coordinate labels */
            fl_font(FL_HELVETICA, 11);
            light ? fl_color(181, 136, 99) : fl_color(240, 217, 181);
            const int bottom_rank = flipped ? 7 : 0;
            const int left_file   = flipped ? 7 : 0;
            if (r == bottom_rank) {
                char c[2] = { (char)('a' + f), 0 };
                fl_draw(c, px + SQ - 12, py + SQ - 4);
            }
            if (f == left_file) {
                char c[2] = { (char)('1' + r), 0 };
                fl_draw(c, px + 3, py + 12);
            }
        }
    }

    int handle(int event) override {
        if (event == FL_PUSH) {
            const int col = (Fl::event_x() - x()) / SQ;
            const int row = (Fl::event_y() - y()) / SQ;
            if (col >= 0 && col < 8 && row >= 0 && row < 8) {
                const int f = flipped ? 7 - col : col;
                const int r = flipped ? row : 7 - row;
                on_square_clicked(r * 8 + f);
            }
            return 1;
        }
        return Fl_Widget::handle(event);
    }

private:
    void draw_piece(int px, int py, char piece) {
        const bool white = color_of(piece) == WHITE;
        const char t = lower_piece(piece);
        const char *text = "";
        char letter[2] = { (char)toupper((unsigned char)t), 0 };

        if (letter_mode) {
            fl_font(FL_HELVETICA_BOLD, SQ * 55 / 100);
            text = letter;
        } else {
            fl_font(FL_HELVETICA, SQ * 80 / 100);
            /* U+265A..U+265F, the solid glyphs, used for both colours */
            switch (t) {
                case KING:   text = "\xE2\x99\x9A"; break;
                case QUEEN:  text = "\xE2\x99\x9B"; break;
                case ROOK:   text = "\xE2\x99\x9C"; break;
                case BISHOP: text = "\xE2\x99\x9D"; break;
                case KNIGHT: text = "\xE2\x99\x9E"; break;
                default:     text = "\xE2\x99\x9F"; break;
            }
        }

        if (white) {
            fl_color(20, 20, 20);               /* outline */
            for (int dx = -1; dx <= 1; dx += 2)
                for (int dy = -1; dy <= 1; dy += 2)
                    fl_draw(text, px + dx, py + dy, SQ, SQ, FL_ALIGN_CENTER);
            fl_color(255, 255, 255);
            fl_draw(text, px, py, SQ, SQ, FL_ALIGN_CENTER);
        } else {
            fl_color(25, 25, 25);
            fl_draw(text, px, py, SQ, SQ, FL_ALIGN_CENTER);
        }
    }
};

/* ---------- UI refresh ---------- */

void rebuild_move_list() {
    std::string text;
    char buf[8];
    for (size_t i = 0; i < played.size(); ++i) {
        move_to_str(played[i], buf);
        if (i % 2 == 0) {
            text += std::to_string(i / 2 + 1);
            text += ". ";
            text += buf;
        } else {
            text += "  ";
            text += buf;
            text += "\n";
        }
    }
    move_buf->text(text.c_str());
    move_view->insert_position(move_buf->length());
    move_view->show_insert_position();
}

void update_ui() {
    std::string s;
    if (status != GAME_ONGOING) {
        s = game_status_text(status, pos.side);
    } else if (thinking) {
        s = pos.side == WHITE ? "White engine thinking..." : "Black engine thinking...";
    } else if (current_mode() == MODE_AI && paused) {
        s = "Paused";
    } else {
        s = pos.side == WHITE ? "White to move" : "Black to move";
        if (in_check(&pos, pos.side)) s += " (check)";
    }
    if (!eval_text.empty()) s += "     |     " + eval_text;
    status_box->copy_label(s.c_str());
    status_box->redraw();

    rebuild_move_list();

    if (thinking) {
        new_btn->deactivate();
        mode_choice->deactivate();
    } else {
        new_btn->activate();
        mode_choice->activate();
    }
    if (can_undo()) undo_btn->activate(); else undo_btn->deactivate();

    pause_btn->label(paused ? "Resume" : "Pause");
    if (current_mode() == MODE_AI && status == GAME_ONGOING) pause_btn->activate();
    else pause_btn->deactivate();
    pause_btn->redraw();

    board_view->redraw();
}

void refresh_state() {
    generate_legal(&pos, &legal);
    status = game_status(&pos);
}

/* ---------- engine on a worker thread ---------- */

struct Job {
    Position pos;
    int depth;
    Move best;
    int score;
};

void apply_move(Move m);

void engine_done(void *p) {                 /* runs on the main thread */
    Job *job = static_cast<Job *>(p);
    thinking = false;
    if (job->best.from >= 0) {
        const int white_cp = job->score * job->pos.side;   /* White's view */
        char buf[48];
        if (white_cp > MATE - 1000)       std::snprintf(buf, sizeof buf, "Eval: White mates");
        else if (white_cp < -MATE + 1000) std::snprintf(buf, sizeof buf, "Eval: Black mates");
        else std::snprintf(buf, sizeof buf, "Eval: %+.2f", white_cp / 100.0);
        eval_text = buf;
        apply_move(job->best);
    } else {
        update_ui();
    }
    delete job;
}

void engine_thread(Job *job) {              /* runs on the worker thread */
    job->best = find_best_move(&job->pos, job->depth, &job->score);
    Fl::awake(engine_done, job);
}

void maybe_start_engine() {
    if (status != GAME_ONGOING || thinking || is_human_turn()) return;
    thinking = true;
    update_ui();

    Job *job = new Job;
    job->pos = pos;
    job->depth = (int)(pos.side == WHITE ? depth_white->value() : depth_black->value());
    job->best = Move{ -1, -1, 0, 0, 0 };
    job->score = 0;
    std::thread(engine_thread, job).detach();
}

void timer_cb(void *) { maybe_start_engine(); }

/* Engine vs engine waits a moment between moves so the game is watchable
 * and the UI (Pause, Flip, ...) stays responsive; other modes start at once. */
void schedule_engine() {
    if (current_mode() == MODE_AI) {
        if (status == GAME_ONGOING && !paused && !thinking)
            Fl::add_timeout(0.4, timer_cb);
    } else {
        maybe_start_engine();
    }
}

/* ---------- game flow ---------- */

void apply_move(Move m) {
    prev_positions.push_back(pos);
    played.push_back(m);
    make_move(&pos, m);
    hist_push(&pos);
    last_from = m.from;
    last_to = m.to;
    selected = -1;
    refresh_state();
    update_ui();
    schedule_engine();
}

void new_game() {
    if (thinking) return;
    Fl::remove_timeout(timer_cb);
    paused = false;
    start_position(&pos);
    hist_reset();
    hist_push(&pos);
    prev_positions.clear();
    played.clear();
    selected = last_from = last_to = -1;
    eval_text.clear();
    flipped = (current_mode() == MODE_BLACK);
    refresh_state();
    update_ui();
    schedule_engine();
}

void pop_one_ply() {
    pos = prev_positions.back();
    prev_positions.pop_back();
    played.pop_back();
    hist_pop();
}

void undo_move() {
    if (!can_undo()) return;
    if (current_mode() == MODE_AI) {            /* stop the game after an undo */
        paused = true;
        Fl::remove_timeout(timer_cb);
    }
    pop_one_ply();
    /* In engine games, also take back the engine's reply so it is the
     * human's turn again. */
    if (current_mode() != MODE_AI && !is_human_turn() && !played.empty()) pop_one_ply();

    if (played.empty()) { last_from = last_to = -1; }
    else { last_from = played.back().from; last_to = played.back().to; }
    selected = -1;
    eval_text.clear();
    refresh_state();
    update_ui();
    schedule_engine();
}

void on_square_clicked(int sq) {
    if (thinking || status != GAME_ONGOING || !is_human_turn()) return;

    if (selected >= 0) {
        Move cand[4];
        int n = 0;
        for (int i = 0; i < legal.count && n < 4; ++i)
            if (legal.moves[i].from == selected && legal.moves[i].to == sq)
                cand[n++] = legal.moves[i];

        if (n > 0) {
            Move chosen = cand[0];
            if (chosen.promotion) {
                const char pc = ask_promotion();
                if (!pc) return;                 /* popup cancelled */
                bool found = false;
                for (int i = 0; i < n; ++i)
                    if (cand[i].promotion == pc) { chosen = cand[i]; found = true; break; }
                if (!found) return;
            }
            apply_move(chosen);
            return;
        }
    }

    if (color_of(pos.board[sq]) == pos.side && has_moves_from(sq)) selected = sq;
    else selected = -1;
    board_view->redraw();
}

/* ---------- widget callbacks ---------- */

void cb_new(Fl_Widget *, void *)     { new_game(); }
void cb_undo(Fl_Widget *, void *)    { undo_move(); }
void cb_pause(Fl_Widget *, void *) {
    paused = !paused;
    if (paused) Fl::remove_timeout(timer_cb);   /* a move in progress still finishes */
    update_ui();
    if (!paused) schedule_engine();
}
void cb_flip(Fl_Widget *, void *)    { flipped = !flipped; board_view->redraw(); }
void cb_mode(Fl_Widget *, void *)    { new_game(); }
void cb_letters(Fl_Widget *, void *) { letter_mode = letters_btn->value() != 0; board_view->redraw(); }

} /* namespace */

int main(int argc, char **argv) {
    Fl::lock();                         /* enable Fl::awake() from the worker */
    Fl::scheme("gtk+");
    engine_init();

    Fl_Double_Window win(BOARD_PX + PANEL_W, BOARD_PX + STATUS_H, "FLTK Chess");

    board_view = new BoardView(0, 0, BOARD_PX, BOARD_PX);

    const int x0 = BOARD_PX + 10;
    mode_choice = new Fl_Choice(x0 + 62, 10, 168, 28, "Mode:");
    mode_choice->add("Play as White|Play as Black|Two players|Engine vs engine");
    mode_choice->value(MODE_WHITE);
    mode_choice->callback(cb_mode);

    depth_white = new Fl_Hor_Value_Slider(x0 + 62, 44, 168, 24, "White AI:");
    depth_white->bounds(1, 6);
    depth_white->step(1);
    depth_white->value(4);

    depth_black = new Fl_Hor_Value_Slider(x0 + 62, 74, 168, 24, "Black AI:");
    depth_black->bounds(1, 6);
    depth_black->step(1);
    depth_black->value(4);

    new_btn = new Fl_Button(x0, 108, 112, 28, "New game");
    new_btn->callback(cb_new);
    undo_btn = new Fl_Button(x0 + 118, 108, 112, 28, "Undo");
    undo_btn->callback(cb_undo);

    flip_btn = new Fl_Button(x0, 142, 112, 28, "Flip board");
    flip_btn->callback(cb_flip);
    letters_btn = new Fl_Check_Button(x0 + 118, 142, 112, 28, "Letter pieces");
    letters_btn->callback(cb_letters);

    pause_btn = new Fl_Button(x0, 176, 230, 28, "Pause");
    pause_btn->callback(cb_pause);

    move_buf = new Fl_Text_Buffer();
    move_view = new Fl_Text_Display(x0, 216, 230, BOARD_PX - 226);
    move_view->buffer(move_buf);
    move_view->textfont(FL_COURIER);
    move_view->textsize(14);

    status_box = new Fl_Box(0, BOARD_PX, BOARD_PX + PANEL_W, STATUS_H);
    status_box->box(FL_FLAT_BOX);
    status_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    status_box->labelsize(14);

    win.end();
    new_game();
    win.show(argc, argv);
    const int rc = Fl::run();
    if (thinking) std::_Exit(rc);       /* don't race a still-running search */
    return rc;
}
