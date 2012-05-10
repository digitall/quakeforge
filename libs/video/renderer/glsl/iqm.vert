uniform mat4 bonemats[80];
attribute vec4 vcolor;
attribute vec4 vweights;
attribute vec4 vbones;
attribute vec2 texcoord;
attribute vec4 vtangent;
attribute vec3 vnormal;
attribute vec3 position;

varying vec3 bitangent;
varying vec3 tangent;
varying vec3 normal;
varying vec2 st;
varying vec4 color;

void
main (void)
{
	mat4        m;
	vec4        q0, qe;
	vec3        sh, sc, tr, v, n, t;

	m  = bonemats[int (vbones.x)] * vweights.x;
	m += bonemats[int (vbones.y)] * vweights.y;
	m += bonemats[int (vbones.z)] * vweights.z;
	m += bonemats[int (vbones.w)] * vweights.w;
	q0 = m[0].yzwx;
	qe = m[1].yzwx;
	sh = m[2].xyz;
	sc = m[3].xyz;

	tr = 2.0 * (q0.w * qe.xyz - qe.w * q0.xyz - cross (qe.xyz, q0.xyz));
	v = position;
	// apply rotation and translation
	v += 2.0 * cross (q0.xyz, cross (q0.xyz, v) + q0.w * v) + tr;
	// apply shear
	v.z += v.y * sh.z + v.x * sh.y;
	v.y += v.x * sh.x;
	// apply scale
	v *= sc;
	// rotate normal (won't bother with shear or scale: not super accurate,
	// but probably good enough
	n = vnormal;
	n += 2.0 * cross (q0.xyz, cross (q0.xyz, n) + q0.w * n);
	// rotate tangent (won't bother with shear or scale: not super accurate,
	// but probably good enough
	t = vtangent.xyz;
	t += 2.0 * cross (q0.xyz, cross (q0.xyz, t) + q0.w * t);
	gl_Position = mvp_mat * vec4 (v, 1.0);
	normal = mat3 (mvp_mat[0].xyz, mvp_mat[1].xyz, mvp_mat[3].xyz) * n;
	tangent = mat3 (mvp_mat[0].xyz, mvp_mat[1].xyz, mvp_mat[3].xyz) * t;
	bitangent = cross (normal, tangent) * vtangent.w;
	color = vcolor;
	st = texcoord;
}
