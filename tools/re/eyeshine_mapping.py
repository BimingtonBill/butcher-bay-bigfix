"""Offline EU 1.1 soft-shadow coordinate/filter model; no game code executes.

MXR+1B0D1E..1B0EEC constructs S,T,0,Q planes from the model-to-camera
matrix, the E6 CImage allocation and CRC_Viewport projection/view area.
The observed views start at engine (top-left) 0,0; GL's bottom-left Y changes.
This models the measured inputs, not unlogged GPU derivatives or texel contents.
"""
import json
import math
import struct

TAPS = ((.5, 1.5), (1.5, -.5), (-.5, -1.5), (-1.5, .5),
        (2.5, 1.5), (1.5, -2.5), (-2.5, -1.5), (-1.5, 2.5),
        (3.5, .5), (.5, -3.5), (-3.5, -.5), (-.5, 3.5),
        (3.5, 2.5), (2.5, -3.5), (-3.5, -2.5), (-2.5, 3.5))


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def engine_coefficients(projection_pixels, view_size, image=(4096, 2048), drawable=(2560, 1440)):
    """SSE scalar rounding/order at 1B0D1E..1B0E0B (identity camera)."""
    px, py = map(f32, projection_pixels)
    vw, vh = view_size
    tw, th = image
    dw, dh = drawable
    rx, ry = f32(f32(dw)*f32(1/tw)), f32(f32(dh)*f32(1/th))
    a = f32(f32(px*.5/dw)*rx)
    c = f32(-f32(f32(py*.5/dh)*ry))
    b = f32(f32(-f32(1-f32(vw/tw)))*.5+.5)
    d = f32(f32(1-f32(vh/th))*.5+.5)
    return a, b, c, d


def project(eye, projection_pixels, view_size, image=(4096, 2048), rounded=False):
    """Generated planes, before the FP's 1e-6 bias; optional SSE rounding."""
    x, y, z = eye
    px, py = projection_pixels
    vw, vh = view_size
    tw, th = image
    if rounded:
        a, b, c, d = engine_coefficients(projection_pixels, view_size, image)
        return (f32(f32(f32(a*x)+f32(b*z))/z),
                f32(f32(f32(c*y)+f32(d*z))/z))
    return ((px * .5 * x + vw * .5 * z) / (tw * z),
            (-py * .5 * y + (th - vh * .5) * z) / (th * z))


def pixel_to_texel(pixel, viewport, image=(4096, 2048), shader_bias=True, full_view=None):
    """Eliminate eye/projection; distinguish full camera view from clipping.

    Default: full view equals the top-left view being simulated. A supplied
    full_view is an engine top-left (L,T,R,B) rectangle, before clipping.
    """
    x, y = pixel
    vx, vy, w, h = viewport
    # MSystem+56EAA..56F3D includes a full-view/clip-center offset in projection.
    left, top, right, bottom = full_view or (0, 0, w, h)
    cx, cy = (left+right)/2, (top+bottom)/2
    px, py = right-left, bottom-top
    uv = project(((x-cx)*2/px, -(y-(1440-cy))*2/py, 1), (px, py), (w, h), image)
    return tuple((v + (1e-6 if shader_bias else 0)) * n for v, n in zip(uv, image))


def footprint(pixel, viewport):
    x, y = pixel_to_texel(pixel, viewport)
    return [(x+dx, y+dy) for dx, dy in TAPS]


def linear_support(texel):
    """Texel indices touched by GL_LINEAR, in texel-edge coordinates."""
    x, y = texel
    ix, iy = math.floor(x-.5), math.floor(y-.5)
    return ((ix, iy), (ix+1, iy), (ix, iy+1), (ix+1, iy+1))


def inside(index, rect):
    x, y = index
    left, bottom, width, height = rect
    return left <= x < left+width and bottom <= y < bottom+height


def sample_depth(x, rho, fixed=False):
    """One-dimensional depth edge, encoded/decoded exactly as 256*r+g.

    Texture centers .5,1.5,... have decoded depth 64 on the left of x=100
    and 192 on the right. This is a synthetic discontinuity, not game data.
    Spec's min=LINEAR/mag=NEAREST transition is lambda=log2(rho)=0.
    """
    def decode(i):
        red, green = ((64/255, 0) if i < 100 else (192/255, 0))
        return 256*red + green
    if fixed or rho <= 1:
        return decode(math.floor(x))
    left = math.floor(x-.5)
    fraction = x-.5-left
    return decode(left)*(1-fraction) + decode(left+1)*fraction


def rejection(depth, reference, distance=10):
    return min(1, abs(depth-reference)*.02*distance)


def report():
    rows = []
    for name, vp, mask in [('off', (0, 0, 2560, 1440), (0, 608, 2560, 1440)),
                           ('on', (0, 360, 1920, 1080), (0, 968, 1920, 1080))]:
        p = (vp[0]+vp[2]/2+.5, vp[1]+vp[3]/2+.5)
        taps = footprint(p, vp)
        rows.append(dict(state=name, viewport=vp, mask_copy=mask,
                         depth_copy=(0, 608, 2560, 1440), center=pixel_to_texel(p, vp),
                         sse_coefficients=engine_coefficients((vp[2],vp[3]),(vp[2],vp[3])),
                         taps=taps, all_in_mask=all(inside(i, mask) for t in taps for i in linear_support(t))))
    return dict(mapping=rows, depth_at_half_tap={
        'nearest': sample_depth(100.004096, 1-1e-7),
        'linear': sample_depth(100.004096, 1+1e-7),
        'fixed_above_transition': sample_depth(100.004096, 1+1e-7, True)},
        limitation='The trace has no measured derivatives or texture contents; visual causation requires confirmation.')


if __name__ == '__main__':
    print(json.dumps(report(), indent=2))
