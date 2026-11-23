#!/usr/bin/env python3
"""Check Nine's Mesa 27 callback offsets against the 32-bit compiler ABI."""
import pathlib
import re
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MESA = ROOT / "mesa"
SOURCE = ROOT / "submodules/mesa9x/win9x/nine/mesa99.c"

context_fields = {
    "destroy": 40,
    "draw_vbo": 44,
    "create_query": 64,
    "destroy_query": 72,
    "begin_query": 76,
    "end_query": 80,
    "get_query_result": 84,
    "create_blend_state": 140,
    "bind_blend_state": 144,
    "delete_blend_state": 148,
    "create_sampler_state": 152,
    "bind_sampler_states": 156,
    "delete_sampler_state": 160,
    "create_rasterizer_state": 164,
    "bind_rasterizer_state": 168,
    "delete_rasterizer_state": 172,
    "create_depth_stencil_alpha_state": 176,
    "bind_depth_stencil_alpha_state": 180,
    "delete_depth_stencil_alpha_state": 184,
    "create_fs_state": 188,
    "bind_fs_state": 192,
    "delete_fs_state": 196,
    "create_vs_state": 200,
    "bind_vs_state": 204,
    "delete_vs_state": 208,
    "create_vertex_elements_state": 248,
    "bind_vertex_elements_state": 252,
    "delete_vertex_elements_state": 256,
    "set_blend_color": 284,
    "set_stencil_ref": 288,
    "set_sample_mask": 292,
    "set_clip_state": 304,
    "set_constant_buffer": 308,
    "set_framebuffer_state": 316,
    "set_polygon_stipple": 324,
    "set_scissor_states": 328,
    "set_viewport_states": 336,
    "set_sampler_views": 340,
    "set_vertex_buffers": 368,
    "resource_copy_region": 392,
    "clear": 404,
    "clear_render_target": 408,
    "clear_depth_stencil": 412,
    "blit": 400,
    "flush": 428,
    "create_sampler_view": 444,
    "sampler_view_destroy": 448,
    "buffer_map": 456,
    "transfer_flush_region": 460,
    "buffer_unmap": 472,
    "texture_map": 476,
    "texture_unmap": 480,
    "buffer_subdata": 484,
    "flush_resource": 560,
}

screen_fields = {
    "destroy": 1740,
    "get_name": 1744,
    "get_vendor": 1748,
    "get_device_vendor": 1756,
    "get_cl_cts_version": 1760,
    "context_create": 1784,
    "is_format_supported": 1792,
    "resource_create": 1804,
    "resource_destroy": 1844,
    "fence_reference": 1852,
    "fence_finish": 1860,
}


def compile_offsets(tempdir, struct_name, fields):
    generated = tempdir / "util/format/u_format_gen.h"
    generated.parent.mkdir(parents=True, exist_ok=True)
    with generated.open("w") as output:
        subprocess.run(
            ["python3", str(MESA / "src/util/format/u_format_table.py"),
             str(MESA / "src/util/format/u_format.yaml"), "--enums"],
            cwd=ROOT, check=True, stdout=output)

    source = tempdir / "offsets.c"
    object_file = tempdir / "offsets.o"
    binary_file = tempdir / "offsets.bin"
    header = "pipe/p_context.h" if struct_name == "pipe_context" else "pipe/p_screen.h"
    source.write_text(
        "#include <stddef.h>\n"
        f'#include "{header}"\n'
        '__attribute__((section(".nvxabi"),used)) const unsigned abi[]={'
        + ",".join(f"offsetof(struct {struct_name}, {field})" for field in fields)
        + "};\n")
    includes = [
        tempdir,
        MESA / "include",
        MESA / "src",
        MESA / "src/gallium/include",
        MESA / "src/gallium/auxiliary",
        MESA / "src/include",
        MESA / "src/compiler",
        MESA / "src/compiler/nir",
        MESA / "src/gallium/drivers/nouveau",
    ]
    command = ["i686-w64-mingw32-gcc"]
    for include in includes:
        command += ["-I", str(include)]
    command += ["-c", str(source), "-o", str(object_file)]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run(
        ["i686-w64-mingw32-objcopy", "-O", "binary", "--only-section=.nvxabi",
         str(object_file), str(binary_file)], check=True)
    data = binary_file.read_bytes()
    values = struct.unpack_from(f"<{len(fields)}I", data)
    return dict(zip(fields, values))


def function_body(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = source.find("{", match.start())
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise AssertionError(f"unterminated function: {name}")


def main():
    with tempfile.TemporaryDirectory(prefix="nvx-nine-ctx-abi-") as temp:
        tempdir = pathlib.Path(temp)
        context = compile_offsets(tempdir, "pipe_context", context_fields)
        screen = compile_offsets(tempdir, "pipe_screen", screen_fields)

    failures = []
    for field, expected in context_fields.items():
        if context[field] != expected:
            failures.append(f"pipe_context.{field}: expected {expected}, compiler says {context[field]}")
    for field, expected in screen_fields.items():
        if screen[field] != expected:
            failures.append(f"pipe_screen.{field}: expected {expected}, compiler says {screen[field]}")

    source = SOURCE.read_text()
    wrappers = {
        "bridge_ctx_set_framebuffer_state": "set_framebuffer_state",
        "bridge_ctx_clear_render_target": "clear_render_target",
        "bridge_ctx_clear_depth_stencil": "clear_depth_stencil",
        "bridge_ctx_clear": "clear",
        "bridge_ctx_create_sampler_view": "create_sampler_view",
        "bridge_ctx_sampler_view_destroy": "sampler_view_destroy",
        "bridge_ctx_set_sampler_views": "set_sampler_views",
        "bridge_ctx_set_constant_buffer": "set_constant_buffer",
        "bridge_ctx_set_vertex_buffers": "set_vertex_buffers",
        "bridge_ctx_draw_vbo": "draw_vbo",
        "bridge_ctx_create_blend_state": "create_blend_state",
        "bridge_ctx_bind_blend_state": "bind_blend_state",
        "bridge_ctx_delete_blend_state": "delete_blend_state",
        "bridge_ctx_create_sampler_state": "create_sampler_state",
        "bridge_ctx_bind_sampler_states": "bind_sampler_states",
        "bridge_ctx_delete_sampler_state": "delete_sampler_state",
        "bridge_ctx_create_rasterizer_state": "create_rasterizer_state",
        "bridge_ctx_bind_rasterizer_state": "bind_rasterizer_state",
        "bridge_ctx_delete_rasterizer_state": "delete_rasterizer_state",
        "bridge_ctx_create_depth_stencil_alpha_state": "create_depth_stencil_alpha_state",
        "bridge_ctx_bind_depth_stencil_alpha_state": "bind_depth_stencil_alpha_state",
        "bridge_ctx_delete_depth_stencil_alpha_state": "delete_depth_stencil_alpha_state",
        "bridge_ctx_create_fs_state": "create_fs_state",
        "bridge_ctx_bind_fs_state": "bind_fs_state",
        "bridge_ctx_delete_fs_state": "delete_fs_state",
        "bridge_ctx_create_vs_state": "create_vs_state",
        "bridge_ctx_bind_vs_state": "bind_vs_state",
        "bridge_ctx_delete_vs_state": "delete_vs_state",
        "bridge_ctx_set_blend_color": "set_blend_color",
        "bridge_ctx_set_stencil_ref": "set_stencil_ref",
        "bridge_ctx_set_sample_mask": "set_sample_mask",
        "bridge_ctx_set_clip_state": "set_clip_state",
        "bridge_ctx_set_polygon_stipple": "set_polygon_stipple",
        "bridge_ctx_set_viewport_states": "set_viewport_states",
        "bridge_ctx_create_query": "create_query",
        "bridge_ctx_destroy_query": "destroy_query",
        "bridge_ctx_begin_query": "begin_query",
        "bridge_ctx_end_query": "end_query",
        "bridge_ctx_get_query_result": "get_query_result",
        "bridge_ctx_resource_copy_region": "resource_copy_region",
        "bridge_ctx_blit": "blit",
        "bridge_ctx_flush": "flush",
        "bridge_ctx_transfer_flush_region": "transfer_flush_region",
        "bridge_ctx_buffer_subdata": "buffer_subdata",
        "bridge_ctx_flush_resource": "flush_resource",
    }
    for wrapper, field in wrappers.items():
        body = function_body(source, wrapper)
        match = re.search(r"M27_FPTR\(ctx,\s*(\d+)", body)
        if not match or int(match.group(1)) != context[field]:
            actual = match.group(1) if match else "missing"
            failures.append(f"{wrapper} -> {field}: source offset {actual}, compiler offset {context[field]}")

    ve_body = function_body(source, "bridge_validate_vertex_elements")
    ve_offsets = [int(value) for value in re.findall(r"M27_FPTR\(ctx,\s*(\d+)", ve_body)]
    expected_ve = [context["create_vertex_elements_state"],
                   context["bind_vertex_elements_state"],
                   context["delete_vertex_elements_state"]]
    if ve_offsets != expected_ve:
        failures.append(f"bridge_validate_vertex_elements: source offsets {ve_offsets}, compiler offsets {expected_ve}")

    for wrapper, field, expected in [
        ("bridge_ctx_buffer_map", "buffer_map", context["buffer_map"]),
        ("bridge_ctx_texture_map", "texture_map", context["texture_map"]),
        ("bridge_ctx_buffer_unmap", "buffer_unmap", context["buffer_unmap"]),
        ("bridge_ctx_texture_unmap", "texture_unmap", context["texture_unmap"]),
    ]:
        body = function_body(source, wrapper)
        match = re.search(r"bridge_ctx_(?:map|unmap)\(ctx,\s*res,\s*level,\s*usage,\s*box,\s*out,\s*(\d+)\)|bridge_ctx_unmap\(ctx,\s*transfer,\s*(\d+)\)", body)
        value = next((int(v) for v in match.groups() if v is not None), None) if match else None
        if value != expected:
            failures.append(f"{wrapper} -> {field}: source offset {value}, compiler offset {expected}")

    expected_screen = re.findall(r"^#define M27_OFFSET_(\w+)\s+(\d+)$", source, re.M)
    for field, value in expected_screen:
        screen_field = field.lower()
        if screen_field in screen and int(value) != screen[screen_field]:
            failures.append(f"pipe_screen.{screen_field}: source offset {value}, compiler offset {screen[screen_field]}")

    if failures:
        print("Nine/Mesa ABI offset mismatches:")
        print("\n".join(f"  {failure}" for failure in failures))
        return 1
    print(f"Nine/Mesa ABI offsets verified: {len(context_fields)} pipe_context and {len(screen_fields)} pipe_screen callbacks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
