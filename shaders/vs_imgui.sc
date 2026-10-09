$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
	// ImGui vertices are 2D screen-space positions; the orthographic
	// projection set on the ImGui view maps them to clip space.
	gl_Position = mul(u_modelViewProj, vec4(a_position.xy, 0.0, 1.0));
	v_color0    = a_color0;
	v_texcoord0 = a_texcoord0;
}
