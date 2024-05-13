#include "canvas.h"
#include "meshes.h" 
#include <time.h>

#define MIN(x, y) (x < y ? x : y)
#define MAX(x, y) (x > y ? x : y)
#define CLAMP(x, y, z) (MAX(MIN(z, y), x))
#define LERP(a, b, c) (a + (b - a) * c)
#define RAND(min, max) (rand() % (max - min) + min)
#define PRINT(...) { printf(__VA_ARGS__); printf("\n"); }
#define PI  3.14159
#define PI2 PI / 2
#define PI4 PI / 4

#define UPSCALE 0.2
#define SPEED 5
#define SENSITIVITY 0.001
#define CAMERA_LOCK PI2 * 0.9
#define FOV PI4
#define AI_ENABLED 1
#define AI_VERBOSE 0

// ---

typedef enum {
  NONE,
  WHITE_PAWN, WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN, WHITE_KING,
  BLACK_PAWN, BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN, BLACK_KING 
} Piece;

typedef struct {
  Piece piece;
  u8    is_mv;
} Slot;

typedef struct {
  u8  move[4];
  i8 score;
} AIMove;

void handle_inputs(GLFWwindow*);
void ai_play();

// ---

Camera cam    = { 0, 0, FOV, 0.1, 100, 0, 0, { 4, 2, 4 }, { 0, 0, -1 }, { 1, 0, 0 }};
Canvas canvas;
mat4 view, proj, blank;
vec3 mouse;
u32 shader;
f32 fps, tick = 0;

Material black_piece = { { 0.20, 0.20, 0.20 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material white_piece = { { 0.70, 0.70, 0.70 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material selec_piece = { { 0.45, 0.30, 0.60 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material alert_piece = { { 0.95, 0.30, 0.30 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material board_top   = { { 1.00, 1.00, 1.00 }, 1, 1, 0.4, 500, 2, 0, 1, 0 };
Material board       = { { 0.60, 0.60, 0.60 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material absolute    = { { 1.00, 1.00, 1.00 }, 1, 1, 0.0, 255, 0, 0, 0, 1 };

Animation anim = { 0, 3, 0 };

Slot game[8][8] = {
  { { BLACK_ROOK }, { BLACK_KNIGHT }, { BLACK_BISHOP }, { BLACK_KING }, { BLACK_QUEEN }, { BLACK_BISHOP }, { BLACK_KNIGHT }, { BLACK_ROOK } },
  { { BLACK_PAWN }, { BLACK_PAWN   }, { BLACK_PAWN   }, { BLACK_PAWN }, { BLACK_PAWN  }, { BLACK_PAWN   }, { BLACK_PAWN   }, { BLACK_PAWN } },
  { { NONE       }, { NONE         }, { NONE         }, { NONE       }, { NONE        }, { NONE         }, { NONE         }, { NONE       } },
  { { NONE       }, { NONE         }, { NONE         }, { NONE       }, { NONE        }, { NONE         }, { NONE         }, { NONE       } },
  { { NONE       }, { NONE         }, { NONE         }, { NONE       }, { NONE        }, { NONE         }, { NONE         }, { NONE       } },
  { { NONE       }, { NONE         }, { NONE         }, { NONE       }, { NONE        }, { NONE         }, { NONE         }, { NONE       } },
  { { WHITE_PAWN }, { WHITE_PAWN   }, { WHITE_PAWN   }, { WHITE_PAWN }, { WHITE_PAWN  }, { WHITE_PAWN   }, { WHITE_PAWN   }, { WHITE_PAWN } },
  { { WHITE_ROOK }, { WHITE_KNIGHT }, { WHITE_BISHOP }, { WHITE_KING }, { WHITE_QUEEN }, { WHITE_BISHOP }, { WHITE_KNIGHT }, { WHITE_ROOK } },
};

i8 selected[2] = { -1, -1 };
u8 anim_move[4];
u8 white_turn = 1;
u8 game_end = 0;

// ---

void main() {
  canvas_init(&canvas, &cam, (CanvasInitConfig) { 1, "Chess" });
  glm_mat4_identity(blank);
  srand(time(0));

  Model* cube = model_create("obj/cube.obj");
  Model* piece_models[] = { 
    model_create("obj/king.obj"),
    model_create("obj/pawn.obj"),
    model_create("obj/knight.obj"),
    model_create("obj/bishop.obj"),
    model_create("obj/rook.obj"),
    model_create("obj/queen.obj"),
  };

  u32 lowres_fbo = canvas_create_FBO(cam.width * UPSCALE, cam.height * UPSCALE, GL_NEAREST, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  generate_proj_mat(cam, proj);
  generate_view_mat(cam, view);

  canvas_create_texture(GL_TEXTURE0, "img/w.ppm",     GL_MIRRORED_REPEAT, GL_MIRRORED_REPEAT, GL_NEAREST, GL_NEAREST);
  canvas_create_texture(GL_TEXTURE1, "img/b.ppm",     GL_MIRRORED_REPEAT, GL_MIRRORED_REPEAT, GL_NEAREST, GL_NEAREST);
  canvas_create_texture(GL_TEXTURE2, "img/board.ppm", GL_MIRRORED_REPEAT, GL_MIRRORED_REPEAT, GL_NEAREST, GL_NEAREST);

  shader = shader_create_program("shd/obj.v", "shd/obj.f");
  
  canvas_uni3f(shader, "PNT_LIGS[0].POS", white_turn * 8, 8, 4);
  canvas_uni3f(shader, "PNT_LIGS[0].COL", 1, 1, 1);
  canvas_uni1f(shader, "PNT_LIGS[0].CON", 1);
  canvas_uni1f(shader, "PNT_LIGS[0].LIN", 0.07);
  canvas_uni1f(shader, "PNT_LIGS[0].QUA", 0.017);
  
  while (!glfwWindowShouldClose(canvas.window)) {
    fps = 1 / (glfwGetTime() - tick);
    tick = glfwGetTime();

    canvas_unim4(shader, "PROJ", proj[0]);
    canvas_unim4(shader, "VIEW", view[0]);

    canvas_set_material(shader, absolute);
    for (u8 row = 0; row < 8; row++) {
      for (u8 col = 0; col < 8; col++) {
        if (!game[row][col].is_mv && !game[row][col].piece) continue;
        canvas_uni3f(shader, "MAT.COL", (f32) row / 10, (f32) col / 10, 0);

        glm_mat4_identity(cube->model);
        glm_translate(cube->model, (vec3) { row + 0.15, 0, col + 0.15 });
        glm_scale(cube->model, (vec3) { 0.7, 0.1, 0.7 });
        model_draw(cube, shader);

        if (!game[row][col].piece) continue;
        Model* piece = piece_models[game[row][col].piece % 6];
        glm_mat4_identity(piece->model);
        glm_translate(piece->model, (vec3) { row + 0.5, 0, col + 0.5 });
        model_draw(piece, shader);
      }
    }

    handle_inputs(canvas.window);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    canvas_set_material(shader, board);
    glm_mat4_identity(cube->model);
    glm_scale(cube->model, (vec3) { 8, 1, 8 });
    glm_translate(cube->model, (vec3) { 0, -1.011, 0 });
    model_draw(cube, shader);

    canvas_set_material(shader, board_top);
    glm_mat4_identity(cube->model);
    glm_scale(cube->model, (vec3) { 8, 0.01, 8 });
    glm_translate(cube->model, (vec3) { 0, -1.001, 0 });
    model_draw(cube, shader);

    for (u8 row = 0; row < 8; row++) {
      for (u8 col = 0; col < 8; col++) {
        if (!game[row][col].piece) {
          if (game[row][col].is_mv) {
            canvas_set_material(shader, selec_piece);
            glm_mat4_identity(cube->model);
            glm_translate(cube->model, (vec3) { row + 0.15, 0, col + 0.15 });
            glm_scale(cube->model, (vec3) { 0.7, 0.1, 0.7 });
            model_draw(cube, shader);
          }
          continue;
        }

        canvas_set_material(shader, game[row][col].piece < 7 ? white_piece : black_piece);
        if (selected[0] == row && selected[1] == col) 
          canvas_set_material(shader, selec_piece);
        else if (game[row][col].is_mv) 
          canvas_set_material(shader, alert_piece);

        glm_mat4_identity(cube->model);
        if (anim.stage && row == anim_move[2] && col == anim_move[3])  {
          if      (anim.stage == 1) glm_translate(cube->model, (vec3) { anim_move[0] + 0.15, anim.pos, anim_move[1] + 0.15 });
          else if (anim.stage == 2) glm_translate(cube->model, (vec3) { LERP(anim_move[0],   anim_move[2], anim.pos) + 0.15, 1, LERP(anim_move[1], anim_move[3], anim.pos) + 0.15 });
          else if (anim.stage == 3) glm_translate(cube->model, (vec3) { row + 0.15, 1 - anim.pos, col + 0.15 });
        }
        else
          glm_translate(cube->model, (vec3) { row + 0.15, 0, col + 0.15 });
        glm_scale(cube->model, (vec3) { 0.7, 0.1, 0.7 });
        model_draw(cube, shader);

        Model* piece = piece_models[game[row][col].piece % 6];
        glm_mat4_identity(piece->model);
        if (anim.stage && row == anim_move[2] && col == anim_move[3])  {
          if      (anim.stage == 1) glm_translate(piece->model, (vec3) { anim_move[0] + 0.5, anim.pos, anim_move[1] + 0.5 });
          else if (anim.stage == 2) glm_translate(piece->model, (vec3) { LERP(anim_move[0],   anim_move[2], anim.pos) + 0.5, 1, LERP(anim_move[1], anim_move[3], anim.pos) + 0.5 });
          else if (anim.stage == 3) glm_translate(piece->model, (vec3) { row + 0.5, 1 - anim.pos, col + 0.5 });
        }
        else
          glm_translate(piece->model, (vec3) { row + 0.5, 0, col + 0.5 });
        model_draw(piece, shader);
      }
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    canvas_set_material(shader, absolute);
    canvas_unim4(shader, "PROJ", blank[0]);
    canvas_unim4(shader, "VIEW", blank[0]);
    canvas_uni3f(shader, "MAT.COL", !white_turn, !white_turn, !white_turn);
    glm_mat4_identity(cube->model);
    glm_scale(cube->model, (vec3) { (f32) cam.height / cam.width * 0.01, 1 * 0.01, 0 });
    glm_translate(cube->model, (vec3) { -0.5, -0.5 });
    model_draw(cube, shader);

    glBlitNamedFramebuffer(0, lowres_fbo, 0, 0, cam.width, cam.height, 0, 0, cam.width * UPSCALE, cam.height * UPSCALE, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitNamedFramebuffer(lowres_fbo, 0, 0, 0, cam.width * UPSCALE, cam.height * UPSCALE, 0, 0, cam.width, cam.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glfwPollEvents();
    glfwSwapBuffers(canvas.window); 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (anim.stage) animation_run(&anim, ((anim.pos * 3 + 1) * 3) / fps);
  }
  
  glfwTerminate();
}

void calc_lateral_diagonal_moves(Slot board[8][8], u8 row, u8 col, c8 mov, u8 max_dis) {
  u8 dir_flags[4] = { 1, 1, 1, 1 };
  u8 _row, _col;

  for (u8 dis = 1; dis <= max_dis; dis++) {
    for (u8 dir = 0; dir < 4; dir++) {
      if (!dir_flags[dir]) continue;

      _row = row + (mov == 'L' ? (dir == 0 ? -dis : dir == 1 ? dis : 0) : (dir == 0 || dir == 1 ? -dis : dir == 2 || dir == 3 ? dis : 0));
      _col = col + (mov == 'L' ? (dir == 2 ? -dis : dir == 3 ? dis : 0) : (dir == 0 || dir == 2 ? -dis : dir == 1 || dir == 3 ? dis : 0));

      if (_row < 0 || _row > 7 || _col < 0 || _col > 7 || (board[_row][_col].piece && board[_row][_col].piece < 7 == board[row][col].piece < 7)) { dir_flags[dir] = 0; continue; }
      if (board[_row][_col].piece && board[_row][_col].piece < 7 != board[row][col].piece < 7) dir_flags[dir] = 0;
      board[_row][_col].is_mv = 1;
    } 
  }
}

void calc_pawn_moves(Slot board[8][8], u8 row, u8 col) {
  i8 dir = board[row][col].piece < 7 ? -1 : 1;

  if (col - 1 >= 0 && board[row + dir][col - 1].piece && board[row + dir][col - 1].piece < 7 != board[row][col].piece < 7) board[row + dir][col - 1].is_mv = 1;
  if (col + 1 <= 7 && board[row + dir][col + 1].piece && board[row + dir][col + 1].piece < 7 != board[row][col].piece < 7) board[row + dir][col + 1].is_mv = 1;

  if (board[row + dir][col].piece) return;
  board[row + dir][col].is_mv = 1;

  if (row != (board[row][col].piece >= 7 ? 1 : 6) || board[row + dir * 2][col].piece) return;
  board[row + dir * 2][col].is_mv = 1;
}

void calc_knight_moves(Slot board[8][8], u8 row, u8 col) {
  u8 _row, _col;

  for (u8 i = 1; i <= 2; i++) {
    for (i8 row_m = -1; row_m <= 1; row_m += 2) {
      for (i8 col_m = -1; col_m <= 1; col_m += 2) {
        _row = row + (i                * row_m);
        _col = col + ((i == 1 ? 2 : 1) * col_m);

        if (_row >= 0 && _row <= 7 && _col >= 0 && _col <= 7 && (!board[_row][_col].piece || board[_row][_col].piece < 7 != board[row][col].piece < 7))
          board[_row][_col].is_mv = 1;
      }
    }
  }
}

void reset_is_mvs(Slot board[8][8]) {
  for (u8 row = 0; row < 8; row++)
    for (u8 col = 0; col < 8; col++)
     board[row][col].is_mv = 0;
}

void copy_board(Slot from[8][8], Slot to[8][8]) {
  for (u8 row = 0; row < 8; row++)
    for (u8 col = 0; col < 8; col++)
      to[row][col].piece = from[row][col].piece;
  reset_is_mvs(to);
}

void print_board(Slot board[8][8], u8 highlight[4]) {
  char names[13][3] = { "  ", "wP", "wH", "wB", "wR", "wQ", "wK", "bP", "bH", "bB", "bR", "bQ", "bK" };
  char mods[3][3]   = { " ",  "*",   "!" };
  PRINT("---------------------------------------------------------");
  for (u8 row = 0; row < 8; row++) {
    printf("|");
    for (u8 col = 0; col < 8; col++) {
      if (row == highlight[0] && col == highlight[1] ||row == highlight[2] && col == highlight[3]) 
        printf(" >");
      else
        printf("  ");
      printf("%s", names[board[row][col].piece]);
      printf("%s", mods[!!(board[row][col].is_mv && board[row][col].piece) + !!board[row][col].is_mv]);
      printf(" |");
    }
    PRINT(" ");
  }
  PRINT("---------------------------------------------------------");
}

Piece move(Slot board[8][8], u8 f_row, u8 f_col, u8 t_row, u8 t_col, u8 perform_checks) {
  Piece movd = board[f_row][f_col].piece;
  Piece capd = board[t_row][t_col].piece;
  board[f_row][f_col].piece = NONE;
  board[t_row][t_col].piece = movd + (movd % 6 == 1 && !(t_row % 7)) * 4;

  if (!perform_checks) return capd;
  white_turn = !white_turn;
  reset_is_mvs(board);
  canvas_uni3f(shader, "PNT_LIGS[0].POS", white_turn * 8, 8, 4);
  if (!capd || (capd && capd % 6)) {
    if (AI_ENABLED && !white_turn) ai_play();
    return capd;
  }
  
  game_end = 1;
  canvas_uni3f(shader, "PNT_LIGS[0].POS", 4, 8, 4);
  glClearColor(0.95/4, 0.3/4, 0.3/4, 1);

  if (AI_ENABLED && !white_turn) 
    glClearColor(0.45/4, 0.3/4, 0.6/4, 1);

  return capd;
}

void calc_movements(Slot board[8][8], u8 row, u8 col, u8 reset) {
  if (reset) reset_is_mvs(board);

  switch (board[row][col].piece % 6) {
    case 0:
      calc_lateral_diagonal_moves(board, row, col, 'L', 1);
      calc_lateral_diagonal_moves(board, row, col, 'D', 1);
      break;
    case 5:
      calc_lateral_diagonal_moves(board, row, col, 'L', 7);
      calc_lateral_diagonal_moves(board, row, col, 'D', 7);
      break;
    case 4:
      calc_lateral_diagonal_moves(board, row, col, 'L', 7);
      break;
    case 3:
      calc_lateral_diagonal_moves(board, row, col, 'D', 7);
      break;
    case 2:
      calc_knight_moves(board, row, col);
      break;
    case 1:
      calc_pawn_moves(board, row, col);
      break;
  }
}
void update_selection(u8 row, u8 col) {
  u8 is_movement = game[row][col].is_mv;
  reset_is_mvs(game);

  vec2 was_selected = { selected[0], selected[1] };
  selected[0] = -1;
  selected[1] = -1;
  if (is_movement)
    move(game, was_selected[0], was_selected[1], row, col, 1);

  if (is_movement || game_end || !game[row][col].piece || game[row][col].piece < 7 != white_turn) return;
  selected[0] = row;
  selected[1] = col;

  calc_movements(game, row, col, 1);
}

void ai_play() {
  AIMove* moves = malloc(sizeof(AIMove) * 218);
  u16 move_i = 0;

  for (u8 f_row = 0; f_row < 8; f_row++) {
    for (u8 f_col = 0; f_col < 8; f_col++) {
      if (game[f_row][f_col].piece < 7) continue;
      calc_movements(game, f_row, f_col, 1);

      for (u8 t_row = 0; t_row < 8; t_row++) {
        for (u8 t_col = 0; t_col < 8; t_col++) {
          if (!game[t_row][t_col].is_mv) continue;

          moves[move_i].move[0] = f_row;
          moves[move_i].move[1] = f_col;
          moves[move_i].move[2] = t_row;
          moves[move_i].move[3] = t_col;
          moves[move_i].score   = 0;

          if (t_row == 7 && game[f_row][f_col].piece == BLACK_PAWN)
            moves[move_i].score += 12;

          Slot board[8][8];
          copy_board(game, board);

          Piece capd = move(board, f_row, f_col, t_row, t_col, 0);
          if (capd)
            moves[move_i].score += capd == WHITE_KING ? 100 : capd % 6;

          for (u8 row = 0; row < 8; row++)
            for (u8 col = 0; col < 8; col++)
              if (board[row][col].piece && board[row][col].piece < 7)
                calc_movements(board, row, col, 0);

          u8 best_opponent = 0;
          for (u8 row = 0; row < 8; row++)
            for (u8 col = 0; col < 8; col++)
              if (board[row][col].is_mv)
                best_opponent = MAX(best_opponent, ((board[row][col].piece == BLACK_KING) ? 100 : board[row][col].piece % 6));
                //                                 -^                                  -^ believe it or not it took me 3 days to find out that I needed those
             
          moves[move_i++].score -= best_opponent ? best_opponent + 2 : 0;
          if (AI_VERBOSE && 0) print_board(board, (u8[4]){ f_row, f_col, t_row, t_col });
          if (AI_VERBOSE) PRINT("Board: %i; AI: %i; Best opponent: %i;", move_i - 1, moves[move_i -1].score, best_opponent);
        }
      }
    }
  }

  u8* best_moves    = malloc(sizeof(u8) * move_i);
  u8  best_moves_i  = 0;
  best_moves[0]     = 0;

  for (u8 i = 0; i < move_i; i++) {
    if (moves[i].score < moves[best_moves[0]].score) continue;
    if (moves[i].score > moves[best_moves[0]].score)
      best_moves_i = 0;
    best_moves[best_moves_i++] = i;
  }
  u8 chosen_move = best_moves[(u8) RAND(0, best_moves_i)];
  if (moves[chosen_move].score != moves[best_moves[0]].score) PRINT("CLEARLY SOMETHIN WENT WRONG");
  if (AI_VERBOSE) printf("%i (Score: %i)\n", chosen_move, moves[chosen_move].score);
  
  move(game, moves[chosen_move].move[0], moves[chosen_move].move[1],moves[chosen_move].move[2], moves[chosen_move].move[3], 1);
  anim_move[0] = moves[chosen_move].move[0];
  anim_move[1] = moves[chosen_move].move[1];
  anim_move[2] = moves[chosen_move].move[2];
  anim_move[3] = moves[chosen_move].move[3];
  animation_run(&anim, 1);
}

void handle_inputs(GLFWwindow* window) {
  // MOVEMENT
  vec3 prompted_move = { 
    (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ?  SPEED / fps : 0) + (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ? -SPEED / fps : 0), 
    (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS ?  SPEED / fps : 0) + (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ? -SPEED / fps : 0), 
    (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ?  SPEED / fps : 0) + (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? -SPEED / fps : 0)
  };

  if (prompted_move[0] || prompted_move[1] || prompted_move[2]) {
    vec3 lateral  = { 0, 0, 0 };
    glm_vec3_scale(cam.rig, prompted_move[0], lateral);
    vec3 frontal  = { 0, 0, 0 };
    glm_vec3_scale(cam.dir, prompted_move[2], frontal);
    vec3 vertical = { 0, prompted_move[1], 0 };

    glm_vec3_add(cam.pos, lateral,  cam.pos);
    glm_vec3_add(cam.pos, frontal,  cam.pos);
    glm_vec3_add(cam.pos, vertical, cam.pos);
    generate_view_mat(cam, view);  
    canvas_uni3f(shader, "CAM", cam.pos[0], cam.pos[1], cam.pos[2]);
  };

  // MISC
  if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) { cam.fov = MIN(cam.fov + PI / 100, FOV); generate_proj_mat(cam, proj); }
  if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) { cam.fov = MAX(cam.fov - PI / 100, 0.1); generate_proj_mat(cam, proj); }
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, 1);

  // MOUSE BUTTON
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE) mouse[2] = 0;
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !mouse[2]) {
    mouse[2] = 1;
    f32* buffer = malloc(sizeof(f32) * 3);
    glReadPixels(cam.width / 2, cam.height / 2, 1, 1, GL_RGB, GL_FLOAT, buffer);
    update_selection(roundf(buffer[0] * 10), roundf(buffer[1] * 10));
  }

  // MOUSE MOVEMENT
  f64 x, y;
  glfwGetCursorPos(window, &x, &y);

  if (!mouse[0]) {
    mouse[0] = x;
    mouse[1] = y;
  }
  if (x == mouse[0] && y == mouse[1]) return;

  cam.yaw  += (x - mouse[0]) * SENSITIVITY;
  cam.pitch = CLAMP(-CAMERA_LOCK, cam.pitch + (mouse[1] - y) * SENSITIVITY, CAMERA_LOCK);

  cam.dir[0] = cos(cam.yaw - PI2) * cos(cam.pitch);
  cam.dir[1] = sin(cam.pitch);
  cam.dir[2] = sin(cam.yaw - PI2) * cos(cam.pitch);
  cam.rig[0] = cos(cam.yaw) * cos(cam.pitch);
  cam.rig[2] = sin(cam.yaw) * cos(cam.pitch);
  glm_normalize(cam.rig);

  generate_view_mat(cam, view);
  mouse[0] = x;
  mouse[1] = y;
}
