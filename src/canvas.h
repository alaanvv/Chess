#include <stdio.h>
#include <glad/glad.h>
#include <cglm/cglm.h>
#include <GLFW/glfw3.h>

#define ASSERT(x, ...) if (!(x)) { printf(__VA_ARGS__); exit(1); }
#define UNI(shd, uni) (glGetUniformLocation(shd, uni))

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef float    f32;
typedef double   f64;
typedef char     c8;

typedef struct {
  u16 width, height;
  f32 fov, near, far, pitch, yaw;
  vec3 pos, dir, rig;
} Camera;

typedef struct {
  GLFWwindow* window;
} Canvas;

typedef struct {
  vec3 col;
  f64  amb, dif, spc, shi;
  i32 s_dif, s_spc, s_emt;
  u8 lig;
} Material;

typedef struct {
  u8  stage, size;
  f32 pos;
} Animation;

typedef struct {
  u8 capture_mouse;
  char* title;
} CanvasInitConfig;

// --- Function

void canvas_init(Canvas* canvas, Camera* cam, CanvasInitConfig config) {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
  cam->width  = mode->width;
  cam->height = mode->height;

  canvas->window = glfwCreateWindow(cam->width, cam->height, config.title, glfwGetPrimaryMonitor(), NULL);
  glfwMakeContextCurrent(canvas->window);
  gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
  glViewport(0, 0, cam->width, cam->height);
  glEnable(GL_DEPTH_TEST);
  glClearColor(0.2, 0.2, 0.2, 1);

  if (config.capture_mouse) glfwSetInputMode(canvas->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// --- Matrix

void generate_proj_mat(Camera cam, mat4 to) {
  glm_mat4_identity(to);
  glm_perspective(cam.fov, (f32) cam.width / cam.height, cam.near, cam.far, to);
}

void generate_view_mat(Camera cam, mat4 to) {
  vec3 target, up;
  glm_cross(cam.rig, cam.dir, up);
  glm_vec3_add(cam.pos, cam.dir, target);
  glm_lookat(cam.pos, target, up, to);
}

// --- Texture

u32 canvas_create_texture(GLenum unit, char path[], GLenum wrap_s, GLenum wrap_t, GLenum min_filter, GLenum mag_filter) {
  FILE* img = fopen(path, "r");
  ASSERT(img != NULL, "Can't open image");
  u16 width, height;
  fscanf(img, "%*s %hi %hi %*i", &width, &height);
  f32* buffer = malloc(sizeof(f32) * width * height * 3);

  for (u32 i = 0; i < width * height * 3; i += 3) {
    fscanf(img, "%f %f %f", &buffer[i], &buffer[i + 1], &buffer[i + 2]);
    buffer[i]     /= 255;
    buffer[i + 1] /= 255;
    buffer[i + 2] /= 255;
  }
  fclose(img); 

  u32 texture;
  glGenTextures(1, &texture);
  glActiveTexture(unit);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     wrap_s);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     wrap_t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_FLOAT, buffer);
  glGenerateMipmap(GL_TEXTURE_2D);

  free(buffer);
  return texture;
}

// --- Object

u32 canvas_create_VBO(u32 size, const void* data, GLenum usage) {
  u32 VBO;
  glGenBuffers(1, &VBO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, size, data, usage);
  return VBO;
}

u32 canvas_create_VAO() {
  u32 VAO;
  glGenVertexArrays(1, &VAO);
  glBindVertexArray(VAO);
  return VAO;
}

u32 canvas_create_EBO() {
  u32 EBO;
  glGenBuffers(1, &EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  return EBO;
}

u32 canvas_create_FBO(u16 width, u16 height, GLenum min, GLenum mag) {
  u32 FBO;
  glGenFramebuffers(1, &FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);

	u32 REN_TEX;
	glGenTextures(1, &REN_TEX);
	glBindTexture(GL_TEXTURE_2D, REN_TEX);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, min);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mag);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, REN_TEX, 0);

  return FBO;
}

void canvas_vertex_attrib_pointer(u8 location, u8 amount, GLenum type, GLenum normalize, u16 stride, void* offset) {
  glVertexAttribPointer(location, amount, type, normalize, stride, offset);
  glEnableVertexAttribArray(location);
}

// --- Shader

u32 shader_create_program(char vertex_path[], char fragment_path[]) {
  u32 create_shader(GLenum type, char path[], char name[]) {
    FILE* file = fopen(path, "r");
    ASSERT(file != NULL, "Can't open %s shader", name);
    i32 success;

    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    rewind(file);

    char shader_source[size];
    fread(shader_source, sizeof(char), size - 1, file);
    shader_source[size - 1] = '\0';
    fclose(file);

    u32 shader = glCreateShader(type);
    glShaderSource(shader, 1, (const char * const *) &(const char *) { shader_source }, NULL);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    ASSERT(success, "Error compiling %s shader", name);
    return shader;
  }

  u32 v_shader = create_shader(GL_VERTEX_SHADER, vertex_path, "vertex");
  u32 f_shader = create_shader(GL_FRAGMENT_SHADER, fragment_path, "fragment");
  u32 shader_program = glCreateProgram();
  glAttachShader(shader_program, v_shader);
  glAttachShader(shader_program, f_shader);
  glLinkProgram(shader_program);
  glDeleteShader(v_shader);
  glDeleteShader(f_shader);

  i32 success;
  glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
  ASSERT(success, "Error linking shaders");

  glUseProgram(shader_program);
  return shader_program;
}

u32 shader_create_program_raw(const char* v_shader_source, const char* f_shader_source) {
  u32 v_shader = glCreateShader(GL_VERTEX_SHADER);
  u32 f_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(v_shader, 1, &v_shader_source, NULL);
  glShaderSource(f_shader, 1, &f_shader_source, NULL);
  glCompileShader(v_shader);
  glCompileShader(f_shader);

  u32 shader_program = glCreateProgram();
  glAttachShader(shader_program, v_shader);
  glAttachShader(shader_program, f_shader);
  glLinkProgram(shader_program);
  glDeleteShader(v_shader);
  glDeleteShader(f_shader);

  glUseProgram(shader_program);
  return shader_program;
}

void canvas_uni1i(u16 s, char u[], i32 v1)                 { glUniform1i(UNI(s, u), v1); }
void canvas_uni1f(u16 s, char u[], f32 v1)                 { glUniform1f(UNI(s, u), v1); }
void canvas_uni2i(u16 s, char u[], i32 v1, i32 v2)         { glUniform2i(UNI(s, u), v1, v2); }
void canvas_uni2f(u16 s, char u[], f32 v1, f32 v2)         { glUniform2f(UNI(s, u), v1, v2); }
void canvas_uni3i(u16 s, char u[], i32 v1, i32 v2, i32 v3) { glUniform3i(UNI(s, u), v1, v2, v3); }
void canvas_uni3f(u16 s, char u[], f32 v1, f32 v2, f32 v3) { glUniform3f(UNI(s, u), v1, v2, v3); }
void canvas_unim4(u16 s, char u[], const f32* v)           { glUniformMatrix4fv(UNI(s, u), 1, GL_FALSE, (const f32*) (v)); }

void canvas_set_material(u32 shader, Material mat) {
  canvas_uni3f(shader, "MAT.COL", mat.col[0], mat.col[1], mat.col[2]);
  canvas_uni1f(shader, "MAT.AMB", mat.amb);
  canvas_uni1f(shader, "MAT.DIF", mat.dif);
  canvas_uni1f(shader, "MAT.SPC", mat.spc);
  canvas_uni1f(shader, "MAT.SHI", mat.shi);
  canvas_uni1i(shader, "MAT.S_DIF", mat.s_dif);
  canvas_uni1i(shader, "MAT.S_SPC", mat.s_spc);
  canvas_uni1i(shader, "MAT.S_EMT", mat.s_emt);
  canvas_uni1i(shader, "MAT.LIG", mat.lig);
}

void animation_run(Animation* anim, f32 rate) {
  anim->pos += rate;
  if (anim->pos < 1) return;

  anim->pos = 0;
  anim->stage += 1;
  if (anim->stage > anim->size)
    anim->stage = 0;
}
