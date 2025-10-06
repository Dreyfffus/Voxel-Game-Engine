#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 outUV;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

layout (buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

layout (push_constant) uniform constants {
	mat4 render_matrix;
	VertexBuffer vertexBuffer;
}push_constants;

void main() {
	Vertex v = push_constants.vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = push_constants.render_matrix *vec4(v.position, 1.0f);
    fragColor = v.color.xyz;
	outUV = vec2(v.uv_x, v.uv_y);
}