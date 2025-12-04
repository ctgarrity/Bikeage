#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;

struct Vertex {
  vec3 position;
  float uv_x;
  vec3 normal;
  float uv_y;
  vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
  Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer InstanceTransformBuffer {
  mat4 transforms[];
};

//push constants block
layout(push_constant) uniform constants
{
  mat4 render_matrix;
  VertexBuffer vertexBuffer;
  InstanceTransformBuffer transformBuffer;
} PushConstants;

void main()
{
  // Load vertex data from device adress
  Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

  // Per-instance model transform
  mat4 model = PushConstants.transformBuffer.transforms[gl_InstanceIndex];

  // Final position
  gl_Position = PushConstants.render_matrix * model * vec4(v.position, 1.0);

  outColor = v.color.xyz;
  outUV.x = v.uv_x;
  outUV.y = v.uv_y;
}
