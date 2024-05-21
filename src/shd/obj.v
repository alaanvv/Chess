#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNrm;
layout (location = 2) in vec2 aTex;

uniform mat4 MODEL;
uniform mat4 VIEW;
uniform mat4 PROJ;
uniform int TEX_CELLS;
uniform int TEX_ACTIVE_CELL;
uniform int TEX_KEEP_ORIENTATION;

out vec3 pos;
out vec3 nrm;
out vec2 tex;

void main() {
  pos = vec3(MODEL * vec4(aPos, 1));
  gl_Position = PROJ * VIEW * MODEL * vec4(aPos, 1);

  nrm = aNrm;

  tex = vec2(aTex.x / TEX_CELLS + float(TEX_ACTIVE_CELL) / TEX_CELLS + 0.0001, aTex.y);
  if (TEX_KEEP_ORIENTATION == 0) tex.t = 1 - tex.t;
}
