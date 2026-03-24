# Rendering Pipeline

## Overview

The voxel renderer uses a multi-pass pipeline model. Each pass draws a category 
of blocks with its own GL state, shader, and sort order. The passes execute in 
strict order because later passes depend on the depth buffer written by earlier ones.

Pass order (must not be reordered):

| # | Pass               | Blocks              | Depth Write | Blending | Sort         |
|---|--------------------|---------------------|-------------|----------|--------------|
| 1 | Solid              | Opaque terrain      | ON          | OFF      | Front→back   |
| 2 | Cutout             | Leaves, plants      | ON          | OFF*     | Back→front   |
| 3 | Translucent Solid  | Ice, glass          | ON          | Alpha    | Front→back   |
| 4 | Water              | Water, lava         | **OFF**     | Alpha    | Back→front   |

\* Cutout has blending enabled in GL but output alpha is 1.0, so it's a no-op.

## Pass 1: Solid

**Blocks:** Stone, dirt, grass, sand, bedrock, logs, sandstone — anything where
`VoxelBlockDef.opaque == true`.

**Shader:** `opaque_ao` (`assets/shaders/*/opaque_ao.*`)

- Samples terrain atlas (`texture0`).
- Multiplies `texel.rgb` by `vertexColor.rgb` (face brightness + skylight from
  mesher) and `colDiffuse.rgb` (ambient tint).
- `vertexColor.a` encodes baked ambient occlusion (0–1 per corner). The shader
  remaps it through `uAoMin`/`uAoCurve` to darken creases without crushing
  highlights.
- Output alpha is forced to `1.0` — solid pixels never blend.

**GL state:**

- Depth test: `GL_LESS` (raylib default, active since `BeginMode3D`).
- Depth write: ON (`glDepthMask GL_TRUE`, raylib default).
- Blending: OFF in practice (raylib's `BLEND_ALPHA` is active but source alpha
  is always 1.0 so blending is a no-op).
- Face cull: ON, back faces (`rlEnableBackfaceCulling`).

**Sort:** Front-to-back (nearest chunk first). This is an early-Z optimization —
the GPU's hierarchical-Z can reject distant fragments before the fragment shader
runs.

**Vertex data:** position (3f), texcoord (2f), color (4ub).
`color.rgb = face_tint * skylight_luminance`.
`color.a = corner AO packed as (ao * 255/3)`, where ao is 0, 1, 2, or 3.

## Pass 2: Cutout (alpha-tested)

**Blocks:** Leaves (opacity=3), tall grass (opacity=0). Any translucent block
with `VoxelBlockDef.opacity <= 1` goes here.

**Shader:** `cutout` (`assets/shaders/cutout.*`)

- Unlike `opaque_ao`, this shader does NOT use `colDiffuse`. It accesses only
  `texColor * vertexColor`.
- Performs alpha test: discards fragments where `texColor.a * vertexColor.a < 0.1`.
- Remaining fragments write alpha but since depth write is ON and blend is
  effectively off, the alpha value has no visible effect.

**GL state:**

- Depth test: `GL_LESS`.
- Depth write: ON. Even though the shader discards some fragments, the fragments
  that survive write depth normally. If depth write were off, solid geometry
  behind leaves would bleed through.
- Blending: OFF in practice (output alpha is 1.0 for surviving fragments).
- Face cull: OFF (`rlDisableBackfaceCulling`). Leaves and plants need both sides
  visible. The alpha test handles the "invisible" parts, not backface culling.

**Sort:** Back-to-front. In practice the sort doesn't matter much for cutout (no
blending), but it's kept consistent with the translucent convention.

**Nuance — why not use the translucent shader here?**
The cutout shader skips `colDiffuse` multiplication. Raylib's `DrawMesh()`
uploads `colDiffuse` from the material color, but the cutout blocks have been
tinted per-vertex already (face brightness * skylight * block tint). Using
`colDiffuse` as well would double-apply the ambient multiplier. The cutout
shader avoids this by only using the vertex color.

## Pass 3: Translucent Solid (depth-write translucent)

**Blocks:** Ice, glass — blocks that are see-through but should act as solid
surfaces for depth purposes. The mesher routes blocks with
`VoxelBlockDef.translucent == true` AND `block_id == 79` (ice) here.

**Shader:** `translucent` (`assets/shaders/*/translucent.*`)

- `finalColor.rgb = texel.rgb * vertexColor.rgb * colDiffuse.rgb`
- `finalColor.a = texel.a * vertexColor.a * colDiffuse.a`
- NO alpha discard. Every fragment survives and writes its blended alpha.

**GL state:**

- Depth test: `GL_LESS`.
- Depth write: ON. This is the key difference from the water pass. Ice writes
  depth so that water (pass 4) behind it is occluded. Without this, water would
  render through ice because water's depth test would pass against a depth
  buffer that ice never wrote to.
- Blending: `BLEND_ALPHA` (`src_alpha, 1-src_alpha`). Raylib's default blend
  mode.
- Face cull: OFF. Ice is see-through from both sides.

**Sort:** Front-to-back. This is unusual for translucent — normally translucent
is sorted back-to-front for correct blending. However, since ice writes depth,
front-to-back gives early-Z rejection for distant ice fragments while still
compositing correctly (the closest ice fragment wins the depth test and writes
its blended color).

**Vertex alpha:** 230/255. Combined with model tint alpha (255) and texture
alpha, this gives ~90% opacity.

### Raylib DrawMesh interaction

`DrawMesh()` sets the shader's `matModel`, `matView`, `matProjection`, and `mvp`
uniforms automatically from the current rlgl matrix stack. Our translucent shader
uses the combined `uniform mat4 mvp` — raylib resolves this via
`SHADER_LOC_MATRIX_MVP` which it sets to `matModelView * matProjection`.

`DrawMesh` also uploads `colDiffuse` from the material's diffuse map color.
`DrawModel` sets this by multiplying the material's base color by the tint Color
passed by the caller:

```
colDiffuse.rgb = material.base.rgb * tint.rgb / 255
colDiffuse.a   = material.base.a   * tint.a   / 255
```

Since our material base color is WHITE (255,255,255,255) from
`LoadModelFromMesh`, `colDiffuse = tint` normalized.

## Pass 4: Water (no-depth-write translucent)

**Blocks:** Water, lava — liquid blocks. The mesher routes all remaining
translucent blocks (opacity > 1, not ice) here.

**Shader:** `translucent` (same as pass 3).

**GL state:**

- Depth test: `GL_LESS`. Water fragments are still depth-tested against the
  existing buffer (solid + cutout + ice). Water behind a wall doesn't render.
- Depth write: **OFF** (`rlDisableDepthMask`). If water wrote depth, a closer
  water surface would block ice behind it. With depth write off, multiple water
  layers at different depths all contribute through alpha blending.
- Blending: `BLEND_ALPHA`.
- Face cull: OFF.

**Sort:** Back-to-front (farthest chunk first). This is essential for correct
alpha compositing. With depth write off, all water fragments pass the depth test
(they only test against solid geometry, not against each other). Back-to-front
order ensures correct painter's algorithm:

```
result = src.rgb * src.a + dst.rgb * (1 - src.a)
```

If sorting were wrong (front-to-back), the nearest water would write first and
farther water behind it would blend with the already-written nearest water,
producing a "washed out" double-blended appearance.

### Face culling — water vs ice boundaries

The mesher only generates water faces where the neighbor is AIR. Water faces
against ice, opaque blocks, and other water are culled. This means water never
produces faces at ice-water boundaries — only ice does. This prevents coplanar
faces that would z-fight between the two render passes.

### Water surface height

When water is exposed to air above (neighbor y+1 == AIR), the mesher lowers the
top face to `0.90 * block height`. This creates the characteristic shallow water
surface. When water is below another block (ice, stone, etc.), it renders at
full height (1.0) to fill the block space.

## Shader / Uniform Pipeline (raylib internals)

When `DrawModel()` is called, raylib internally:

1. Multiplies the caller's `tint` Color into the material's diffuse map color.
   This becomes the material's effective color.

2. Calls `DrawMesh()` for each mesh in the model.

3. `DrawMesh()` binds the shader and uploads uniforms:
   - `colDiffuse` (`SHADER_LOC_COLOR_DIFFUSE`): material color as vec4,
     normalized to 0–1 range.
   - `matModel` (`SHADER_LOC_MATRIX_MODEL`): the model transform (position +
     scale from DrawModel's arguments, combined with rlgl's internal matrix
     stack from BeginMode3D).
   - `matView` (`SHADER_LOC_MATRIX_VIEW`): current modelview matrix from rlgl
     (set by BeginMode3D to the camera view).
   - `matProjection` (`SHADER_LOC_MATRIX_PROJECTION`): current projection matrix
     from rlgl.
   - `mvp` (`SHADER_LOC_MATRIX_MVP`): `matModel * matView * matProjection`. Only
     set if the shader declares a uniform named "mvp".
   - `texture0` (`SHADER_LOC_MAP_DIFFUSE`): the terrain atlas texture.

4. Binds the mesh VBOs and issues a `glDrawArrays` call.

Raylib resolves shader uniform locations by name at `LoadShader()` time. Known
names (`"mvp"`, `"texture0"`, `"colDiffuse"`, etc.) are automatically mapped to
`SHADER_LOC_*` constants. Unknown names must be looked up manually with
`GetShaderLocation()`.

The `opaque_ao` shader uses custom uniforms `uAoMin` and `uAoCurve` that raylib
doesn't auto-resolve — these are set explicitly via `GetShaderLocation()` +
`SetShaderValue()` at shader load time.

## Blend Mode Notes

Raylib's `BLEND_ALPHA` (the default) maps to:

```
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
glBlendEquation(GL_FUNC_ADD)
```

This means:

```
result = src_color * src_alpha + dst_color * (1 - src_alpha)
```

For ice (src_alpha ~0.9): mostly shows ice color with a hint of whatever is
behind it.

For water (src_alpha ~0.47): the background shows through strongly, tinted by
the water color.

The blend mode is global GL state — it applies to all draw calls until changed.
We don't explicitly set it because raylib's default is `BLEND_ALPHA` and that's
what we need. For additive or multiplicative blending, wrap with
`BeginBlendMode()` / `EndBlendMode()`.

## Face Culling Rules (mesher)

| Block | Neighbor        | Face rendered? |
|-------|-----------------|----------------|
| Solid | Same type       | No             |
| Solid | Opaque          | No             |
| Solid | Air/translucent | Yes            |
| Cutout| Opaque          | No             |
| Cutout| Anything else   | Yes            |
| Ice   | Same type (ice) | No             |
| Ice   | Opaque          | No             |
| Ice   | Air/water/other | Yes            |
| Water | Air             | Yes            |
| Water | Anything else   | No             |

Water never generates faces at ice-water boundaries. Only ice does. This
prevents coplanar faces between the two render passes that would z-fight.


## Notes

Sort is done using `qsort` function, which according to the docs has no
particular requeriment of being done using quicksort as the name could
lead to believe but the function tends to be very optimized.
