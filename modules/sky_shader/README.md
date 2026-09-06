# sky_shader

Adds a `shader_type sky;` shader mode and a **`ShaderSky`** resource, so a sky can be
driven entirely by a custom shader (like Godot 4's sky shaders) on this Godot 3.5 fork.

## How it works

`ShaderSky` holds a `ShaderMaterial` whose shader starts with `shader_type sky;`.

* **Visible sky** — the shader runs **once per screen pixel** in the sky pass. Sharp,
  full-res, no equirect projection distortion. Roughly the same cost as a camera-locked
  dome mesh.
* **Lighting** — separately, a small (default 128 px) equirect panorama is baked from the
  shader, and `sky->radiance` / `sky->irradiance` are re-derived from it through the
  existing dual-paraboloid convolution but **reusing persistent GL textures + FBOs** (no
  per-bake allocation), so the sky still lights the scene / shows in reflection probes /
  `WorldEnvironment` ambient like a `PanoramaSky`. The re-bake is also **coalesced to
  ≤ ~2 Hz** regardless of trigger, so a per-frame day/night script that calls
  `set_shader_param` every frame will not stall the frame rate. In `PROCESS_MODE_ON_CHANGE`
  a static sky bakes exactly once. Ambient light lags the visible sky slightly as a result.
  (GLES3 desktop uses the texture-array radiance path; the plain-2D/mobile path falls back
  to the slower full rebuild.)
* **GLES3 only.** Under GLES2 `sky_set_material` is a warn-once no-op; use
  `PanoramaSky` / `ProceduralSky` there.

## Usage

1. Create a `Shader` starting with `shader_type sky;` and put it in a `ShaderMaterial`.
2. Create a `ShaderSky` resource, assign the material to its `sky_material`.
3. On a `WorldEnvironment`, set `Environment.background_mode` to *Sky* (or *Color+Sky*)
   and `Environment.background_sky` to the `ShaderSky`.
4. Every `uniform` in the shader is editable on the `ShaderMaterial`, in the inspector and
   at runtime via `sky_material.set_shader_param("name", value)`.

### Shader built-ins (`fragment` function)

| Built-in | Type | Meaning |
|---|---|---|
| `EYEDIR` | `vec3` in | world-space view direction for this pixel |
| `SKY_COORDS` | `vec2` in | equirect UV; `.y` 0 = up, 0.5 = horizon, 1 = down |
| `POSITION` | `vec3` in | camera world position |
| `TIME` | `float` in | seconds |
| `AT_CUBEMAP_PASS` | `bool` in | true only during the low-res radiance bake |
| `LIGHT0..3_ENABLED` | `bool` in | whether the Nth `DirectionalLight` exists |
| `LIGHT0..3_DIRECTION` | `vec3` in | travel direction of that light |
| `LIGHT0..3_COLOR` | `vec3` in | that light's color |
| `LIGHT0..3_ENERGY` | `float` in | that light's energy |
| `COLOR` | `vec3` out | sky color (write this) |
| `ALPHA` | `float` out | optional |

Use `AT_CUBEMAP_PASS` to skip expensive detail (sharp sun disc, star field, dense cloud
raymarch) during the radiance bake — it only feeds blurred ambient anyway.

### Example

```glsl
shader_type sky;

uniform vec3 top_color : hint_color = vec3(0.2, 0.4, 0.9);
uniform vec3 horizon_color : hint_color = vec3(0.8, 0.85, 0.9);
uniform float sun_size : hint_range(0.0, 0.5) = 0.02;

void fragment() {
	float h = clamp(EYEDIR.y, 0.0, 1.0);
	vec3 sky = mix(horizon_color, top_color, h);

	if (LIGHT0_ENABLED) {
		float d = dot(normalize(EYEDIR), normalize(-LIGHT0_DIRECTION));
		sky += LIGHT0_COLOR * LIGHT0_ENERGY * smoothstep(1.0 - sun_size, 1.0, d);
	}

	COLOR = sky;
}
```
