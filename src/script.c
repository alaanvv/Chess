#include "canvas.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define UPSCALE 0.15
#define FULLSCREEN 1
#define SCREEN_SIZE 1
#define SPEED 5
#define FOV PI4
#define SENSITIVITY 0.001
#define CAMERA_LOCK PI2 * 0.9
#define BUFFER_SIZE 1024
#define AI_VERBOSE 0

// ---

typedef enum { 
  NONE,
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING 
} Piece;

typedef struct {
  Piece piece;
  u8    is_mv;
} Slot;

typedef struct {
  u8  move[4];
  i8 score;
} AIMove;

Piece move(Slot board[8][8], u8 f_row, u8 f_col, u8 t_row, u8 t_col, u8 perform_checks);
void  handle_inputs(GLFWwindow*);
void* wait_play();
void  ai_play();

// ---

Camera cam = { FOV, 0.1, 100, { 4, 1, 4 } };
vec3 mouse;
u32 shader;
f32 fps = 1, tick = 0;

Material board       = { { 0.60, 0.60, 0.60 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material board_top   = { { 1.00, 1.00, 1.00 }, 1, 1, 0.5, 255, 2, 0, 1, 0 };
Material black_piece = { { 0.20, 0.20, 0.20 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material white_piece = { { 0.70, 0.70, 0.70 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material selec_piece = { { 0.45, 0.30, 0.60 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material alert_piece = { { 0.95, 0.30, 0.30 }, 1, 1, 0.0, 255, 0, 0, 1, 0 };
Material absolute    = { { 1.00, 1.00, 1.00 }, 1, 1, 0.0, 255, 0, 0, 0, 1 };

PntLig light = { { 1, 1, 1 }, { 8, 8, 4 }, 1, 0.07, 0.017 };

Animation anim = { 0, 3, 0 };
u8 anim_move[4];

// ---

Slot game[8][8] = {
  { { B_ROOK }, { B_KNIGHT }, { B_BISHOP }, { B_KING }, { B_QUEEN }, { B_BISHOP }, { B_KNIGHT }, { B_ROOK } },
  { { B_PAWN }, { B_PAWN   }, { B_PAWN   }, { B_PAWN }, { B_PAWN  }, { B_PAWN   }, { B_PAWN   }, { B_PAWN } },
  { { NONE   }, { NONE     }, { NONE     }, { NONE   }, { NONE    }, { NONE     }, { NONE     }, { NONE   } },
  { { NONE   }, { NONE     }, { NONE     }, { NONE   }, { NONE    }, { NONE     }, { NONE     }, { NONE   } },
  { { NONE   }, { NONE     }, { NONE     }, { NONE   }, { NONE    }, { NONE     }, { NONE     }, { NONE   } },
  { { NONE   }, { NONE     }, { NONE     }, { NONE   }, { NONE    }, { NONE     }, { NONE     }, { NONE   } },
  { { W_PAWN }, { W_PAWN   }, { W_PAWN   }, { W_PAWN }, { W_PAWN  }, { W_PAWN   }, { W_PAWN   }, { W_PAWN } },
  { { W_ROOK }, { W_KNIGHT }, { W_BISHOP }, { W_KING }, { W_QUEEN }, { W_BISHOP }, { W_KNIGHT }, { W_ROOK } },
};

i8 selected[2] = { -1, -1 };
u8 white_turn = 1;
u8 control_white;
u8 game_end = 0;
i8 game_mode = -1;
c8 input_buffer[30];
i8 input_size = -1;
c8 ip[30];
u32 port;

// ---

struct sockaddr_in serv_addr;
c8 buffer[BUFFER_SIZE];
i32 sock;
pthread_t thread_id;

// ---

void main() {
  canvas_init(&cam, (CanvasInitConfig) { "Chess", 1, FULLSCREEN, SCREEN_SIZE });
  srand(time(0));

  Material* m_cube[]  = { &board, &board_top, &selec_piece, &absolute };
  Material* m_piece[] = { &black_piece, &white_piece, &selec_piece, &alert_piece, &absolute };
  Model* cube = model_create("obj/cube.obj", 1, m_cube);
  Model* hud = model_create("obj/hud.obj", 1, m_cube);
  Model* piece_models[] = { 
    model_create("obj/king.obj",   1, m_piece),
    model_create("obj/pawn.obj",   1, m_piece),
    model_create("obj/knight.obj", 1, m_piece),
    model_create("obj/bishop.obj", 1, m_piece),
    model_create("obj/rook.obj",   1, m_piece),
    model_create("obj/queen.obj",  1, m_piece),
  };

  u32 lowres_fbo = canvas_create_FBO(cam.width * UPSCALE, cam.height * UPSCALE, GL_NEAREST, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  canvas_create_texture(GL_TEXTURE0, "img/w.ppm",     TEXTURE_DEFAULT);
  canvas_create_texture(GL_TEXTURE1, "img/b.ppm",     TEXTURE_DEFAULT);
  canvas_create_texture(GL_TEXTURE2, "img/board.ppm", TEXTURE_DEFAULT);
  canvas_create_texture(GL_TEXTURE3, "img/font.ppm",  TEXTURE_DEFAULT);

  shader = shader_create_program("shd/obj.v", "shd/obj.f");
  generate_proj_mat(&cam, shader);
  generate_view_mat(&cam, shader);

  canvas_set_pnt_lig(shader, light, 0);

  if (game_mode == 1 && !control_white)
    pthread_create(&thread_id, NULL, wait_play, NULL);

  while (!glfwWindowShouldClose(cam.window)) {
    if (game_mode == 1 && game_end && control_white == white_turn) {
      canvas_uni3f(shader, "PNT_LIGS[0].POS", 4, 4, 4);
      glClearColor(0.95/4, 0.3/4, 0.3/4, 1);
    }

    canvas_set_material(shader, absolute);
    for (u8 row = 0; row < 8; row++) {
      for (u8 col = 0; col < 8; col++) {
        canvas_uni3f(shader, "MAT.COL", (f32) row / 10, (f32) col / 10, 0);
        if (game[row][col].piece) {
          Model* piece = piece_models[game[row][col].piece % 6];
          model_bind(piece, shader, 0);
          glm_translate(piece->model, (vec3) { row + 0.5, 0, col + 0.5 });
          model_draw(piece, shader);
        }
        else if (game[row][col].is_mv) {
          model_bind(cube, shader, 0);
          glm_translate(cube->model, (vec3) { row + 0.15, 0, col + 0.15 });
          glm_scale(cube->model, (vec3) { 0.7, 0.1, 0.7 });
          model_draw(cube, shader);
        }
      }
    }

    handle_inputs(cam.window);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    model_bind(cube, shader, 1);
    glm_scale(cube->model, (vec3) { 8, 1, 8 });
    glm_translate(cube->model, (vec3) { 0, -1.011, 0 });
    model_draw(cube, shader);

    model_bind(cube, shader, 2);
    glm_scale(cube->model, (vec3) { 8, 0.01, 8 });
    glm_translate(cube->model, (vec3) { 0, -1.001, 0 });
    model_draw(cube, shader);

    for (u8 row = 0; row < 8; row++) {
      for (u8 col = 0; col < 8; col++) {
        if (!game[row][col].piece) {
          if (game[row][col].is_mv) {
            model_bind(cube, shader, 3);
            glm_translate(cube->model, (vec3) { row + 0.15, 0, col + 0.15 });
            glm_scale(cube->model, (vec3) { 0.7, 0.1, 0.7 });
            model_draw(cube, shader);
          }
          continue;
        }

        Model* piece = piece_models[game[row][col].piece % 6];
        model_bind(piece, shader, game[row][col].is_mv ? 4 : (selected[0] == row && selected[1] == col ? 3 : (game[row][col].piece < 7 ? 2 : 1)));
        if (anim.stage && row == anim_move[2] && col == anim_move[3])  {
          if      (anim.stage == 1) glm_translate(piece->model, (vec3) { anim_move[0], anim.pos, anim_move[1] });
          else if (anim.stage == 2) glm_translate(piece->model, (vec3) { LERP(anim_move[0],   anim_move[2], anim.pos), 1, LERP(anim_move[1], anim_move[3], anim.pos) });
          else if (anim.stage == 3) glm_translate(piece->model, (vec3) { row, 1 - anim.pos, col });
        }
        else glm_translate(piece->model, (vec3) { row, 0, col });
        glm_translate(piece->model, (vec3) { 0.5, 0, 0.5 });
        model_draw(piece, shader);
      }
    }

    glBlitNamedFramebuffer(0, lowres_fbo, 0, 0, cam.width, cam.height, 0, 0, cam.width * UPSCALE, cam.height * UPSCALE, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitNamedFramebuffer(lowres_fbo, 0, 0, 0, cam.width * UPSCALE, cam.height * UPSCALE, 0, 0, cam.width, cam.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    use_screen_space(&cam, shader, 1);
    if (game_mode == -1) {
      model_bind(hud, shader, 7);
      glm_translate(hud->model, (vec3) { -0.75, 0.35, 0 });
      glm_scale(hud->model, (vec3) { (f32) 1 / cam.width * 100, (f32) 1 / cam.width * 100 * cam.width / cam.height, 0 });
      canvas_render_text(shader, "1 - PVP LOCAL", 3, hud, 1.1);

      model_bind(hud, shader, 7);
      glm_translate(hud->model, (vec3) { -0.75, 0, 0 });
      glm_scale(hud->model, (vec3) { (f32) 1 / cam.width * 100, (f32) 1 / cam.width * 100 * cam.width / cam.height, 0 });
      canvas_render_text(shader, "2 - PVP ONLINE", 3, hud, 1.1);

      model_bind(hud, shader, 7);
      glm_translate(hud->model, (vec3) { -0.75, -0.35, 0 });
      glm_scale(hud->model, (vec3) { (f32) 1 / cam.width * 100, (f32) 1 / cam.width * 100 * cam.width / cam.height, 0 });
      canvas_render_text(shader, "3 - PVM", 3, hud, 1.1);
    }
    else if (game_mode == -2) {
      model_bind(hud, shader, 7);
      glm_translate(hud->model, (vec3) { -0.75, 0.35, 0 });
      glm_scale(hud->model, (vec3) { (f32) 1 / cam.width * 100, (f32) 1 / cam.width * 100 * cam.width / cam.height, 0 });
      canvas_render_text(shader, "HOST:", 3, hud, 1.1);

      model_bind(hud, shader, 7);
      glm_translate(hud->model, (vec3) { -0.75, 0, 0 });
      glm_scale(hud->model, (vec3) { (f32) 1 / cam.width * 100, (f32) 1 / cam.width * 100 * cam.width / cam.height, 0 });
      canvas_render_text(shader, input_buffer, 3, hud, 1.1);
    }
    else {
      model_bind(cube, shader, 7);
      canvas_uni3f(shader, "MAT.COL", !white_turn, !white_turn, !white_turn);
      glm_scale(cube->model, (vec3) { (f32) 1 / cam.width * 20, (f32) 1 / cam.width * 20 * cam.width / cam.height, 0 });
      glm_translate(cube->model, (vec3) { -0.5, -0.5, 0 });
      model_draw(cube, shader);
    }
    use_screen_space(&cam, shader, 0);

    glfwPollEvents();
    update_fps(&fps, &tick);
    glfwSwapBuffers(cam.window); 
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

Piece move(Slot board[8][8], u8 f_row, u8 f_col, u8 t_row, u8 t_col, u8 perform_checks) {
  Piece movd = board[f_row][f_col].piece;
  Piece capd = board[t_row][t_col].piece;
  board[f_row][f_col].piece = NONE;
  board[t_row][t_col].piece = movd + (movd % 6 == 1 && !(t_row % 7)) * 4;

  if (!perform_checks) return capd;
  white_turn = !white_turn;
  reset_is_mvs(board);
  canvas_uni3f(shader, "PNT_LIGS[0].POS", white_turn * 8, 8, 4);

  if (capd == W_KING || capd == B_KING) {
    game_end = 1;
    canvas_uni3f(shader, "PNT_LIGS[0].POS", 4, 4, 4);

    glClearColor(0.95/4, 0.3/4, 0.3/4, 1);
    if (game_mode == 0 || (game_mode == 1 && control_white != white_turn) || (game_mode == 2 && !white_turn)) 
      glClearColor(0.45/4, 0.3/4, 0.6/4, 1);

    return capd;
  }

  else {
    if      (game_mode == 2 && !white_turn) 
      ai_play();
    else if (game_mode == 1 && control_white != white_turn) 
      pthread_create(&thread_id, NULL, wait_play, NULL);
    return capd;
  }
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
  if (game_mode == 1 && white_turn != control_white) return;

  u8 is_movement = game[row][col].is_mv;
  reset_is_mvs(game);

  vec2 was_selected = { selected[0], selected[1] };
  selected[0] = -1;
  selected[1] = -1;
  if (is_movement) {
    if (game_mode == 1) {
      sock = socket(AF_INET, SOCK_STREAM, 0);
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons(port);
      inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

      c8 body[7];
      sprintf(body, "m%c%i%i%i%i", control_white ? 'w' : 'b', (u8) was_selected[0], (u8) was_selected[1], row, col);

      ASSERT(connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) >= 0, "Connection failed");
      send(sock, body, 6, 0);
      close(sock);
    }
    move(game, was_selected[0], was_selected[1], row, col, 1);
  }

  if (is_movement || game_end || !game[row][col].piece || game[row][col].piece < 7 != white_turn || (game_mode == 1 && game[row][col].piece < 7 != control_white)) return;
  selected[0] = row;
  selected[1] = col;

  calc_movements(game, row, col, 1);
}

void char_callback(GLFWwindow* window, u32 codepoint) {
  if (input_size == 29) return;
  if (input_size < 0) {
    input_size++;
    return;
  }
  input_buffer[input_size] = codepoint;
  input_size++;
  input_buffer[input_size] = '\0';
}

void key_callback(GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 mods) {
  if (key == 259 && action == GLFW_PRESS) {
    input_size = MAX(0, input_size - 1);
    input_buffer[input_size] = '\0';
  }
  if (key == 258 && action == GLFW_PRESS) {
    input_size = -1;
    input_buffer[0] = '\0';
    game_mode = -1;
  }
  if (key == 257 && action == GLFW_PRESS) {
    //c8* colon = strchr(input_buffer, ':');
    //ASSERT(colon, "Invalid host");

    //*colon = '\0';
    //strncpy(ip, input_buffer, strlen(input_buffer));

    //port = (u32) strtoul(colon + 1, NULL, 10);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(80);
    inet_pton(AF_INET, "35.226.206.236", &serv_addr.sin_addr);

    ASSERT(connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) >= 0, "Connection failed");
    ASSERT(send(sock, "c", 1, 0) != -1, "Send err");

    ASSERT(read(sock, buffer, BUFFER_SIZE - 1) > 0, "Server sent nothing");
    if      (buffer[0] == 'F') return;
    else if (buffer[0] == 'B') control_white = 0;
    else if (buffer[0] == 'W') control_white = 1;
    close(sock);

    game_mode = 1;
  }
}

void handle_inputs(GLFWwindow* window) {
  // MOVEMENT
  vec3 prompted_move = { 
    (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ?  SPEED / fps : 0) + (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ? -SPEED / fps : 0), 
    (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS ?  SPEED / fps : 0) + (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ? -SPEED / fps : 0), 
    (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ?  SPEED / fps : 0) + (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? -SPEED / fps : 0)
  };

  if (game_mode >= 0 && (prompted_move[0] || prompted_move[1] || prompted_move[2])) {
    vec3 lateral  = { 0, 0, 0 };
    glm_vec3_scale(cam.rig, prompted_move[0], lateral);
    vec3 frontal  = { 0, 0, 0 };
    glm_vec3_scale(cam.dir, prompted_move[2], frontal);
    vec3 vertical = { 0, prompted_move[1], 0 };

    glm_vec3_add(cam.pos, lateral,  cam.pos);
    glm_vec3_add(cam.pos, frontal,  cam.pos);
    glm_vec3_add(cam.pos, vertical, cam.pos);
    generate_view_mat(&cam, shader);  
    canvas_uni3f(shader, "CAM", cam.pos[0], cam.pos[1], cam.pos[2]);
  };

  // MISC
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, 1);
  if (game_mode == -1 && glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) game_mode = 0;
  if (game_mode == -1 && glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
    game_mode = -2;
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    return;
  }
  if (game_mode == -1 && glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) game_mode = 2;

  // MOUSE BUTTON
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE) mouse[2] = 0;
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !mouse[2]) {
    mouse[2] = 1;
    f32* buffer = malloc(sizeof(f32) * 3);
    glReadPixels(cam.width / 2, cam.height / 2, 1, 1, GL_RGB, GL_FLOAT, buffer);
    update_selection(roundf(buffer[0] * 10), roundf(buffer[1] * 10));
  }

  // MOUSE MOVEMENT
  if (game_mode < 0) return;
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

  generate_view_mat(&cam, shader);
  mouse[0] = x;
  mouse[1] = y;
}


void* wait_play() {
  while (1) {
    sleep(1);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    ASSERT(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0, "Connection failed");
    send(sock, "r", 1, 0);

    i32 valread = read(sock, buffer, BUFFER_SIZE - 1);
    ASSERT(valread > 0, "Server returned nothing");
    if   (buffer[0] == 'w' && !control_white || buffer[0] == 'b' && control_white) {
      move(game, (i8) buffer[1] - '0', (i8) buffer[2] - '0', (i8) buffer[3] - '0', (i8) buffer[4] - '0', 1);
      anim_move[0] = (i8) buffer[1] - '0';
      anim_move[1] = (i8) buffer[2] - '0';
      anim_move[2] = (i8) buffer[3] - '0';
      anim_move[3] = (i8) buffer[4] - '0';
      animation_start(&anim);
      break;
    }
    close(sock);
  }
  return NULL;
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

          if (t_row == 7 && game[f_row][f_col].piece == B_PAWN)
            moves[move_i].score += 12;

          Slot board[8][8];
          copy_board(game, board);

          Piece capd = move(board, f_row, f_col, t_row, t_col, 0);
          if (capd)
            moves[move_i].score += capd == W_KING ? 100 : capd % 6;

          for (u8 row = 0; row < 8; row++)
            for (u8 col = 0; col < 8; col++)
              if (board[row][col].piece && board[row][col].piece < 7)
                calc_movements(board, row, col, 0);

          u8 best_opponent = 0;
          for (u8 row = 0; row < 8; row++)
            for (u8 col = 0; col < 8; col++)
              if (board[row][col].is_mv)
                best_opponent = MAX(best_opponent, ((board[row][col].piece == B_KING) ? 100 : board[row][col].piece % 6));
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
  animation_start(&anim);
}
