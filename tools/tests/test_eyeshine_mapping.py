"""Static simulation only; synthetic pixels, no game process or game writes."""
import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location('mapping', Path(__file__).parents[1] / 're/eyeshine_mapping.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


@pytest.mark.parametrize('vp,mask', [((0, 0, 2560, 1440), (0, 608, 2560, 1440)),
                                    ((0, 360, 1920, 1080), (0, 968, 1920, 1080))])
def test_pixel_centers_match_both_copy_translations(vp, mask):
    depth = (0, 608, 2560, 1440)
    for x in (.5, 50.5, vp[2]/2+.5, vp[2]-.5):
        for y in (vp[1]+.5, vp[1]+50.5, vp[1]+vp[3]-.5):
            texel = m.pixel_to_texel((x, y), vp, shader_bias=False)
            assert texel == pytest.approx((x, y+608))
            assert m.inside(tuple(int(t) for t in texel), mask)
            assert m.inside(tuple(int(t) for t in texel), depth)
    # A normalized epsilon is a tiny fraction of a pixel, not a 25% scale.
    a = m.pixel_to_texel((50.5, vp[1]+50.5), vp)
    assert a == pytest.approx((50.504096, vp[1]+658.502048))


def test_full_kernel_interior_and_uncopied_view_edge():
    for row in m.report()['mapping']:
        assert len(row['taps']) == 16
        assert row['all_in_mask']
        vp, rect = row['viewport'], row['mask_copy']
        edge = (vp[2]-.5, vp[1]+vp[3]/2+.5)
        assert any(not m.inside(i, rect) for t in m.footprint(edge, vp) for i in m.linear_support(t))
        interior = (vp[2]-4.5, vp[1]+vp[3]/2+.5)
        assert all(m.inside(i, rect) for t in m.footprint(interior, vp) for i in m.linear_support(t))


def test_unity_lod_switch_changes_rejection_without_pixel_motion():
    reference = m.sample_depth(100.504096, 1-1e-7)
    tap = 100.004096  # center + a shipped half-integer offset + shader epsilon
    below = m.rejection(m.sample_depth(tap, 1-1e-7), reference)
    above = m.rejection(m.sample_depth(tap, 1+1e-7), reference)
    assert below == 0
    assert above == 1  # synthetic depth edge: neighboring mask is rejected
    assert m.rejection(m.sample_depth(tap, 1+1e-7, fixed=True), reference) == below


def test_linear_decoder_is_linear_but_rejection_is_not():
    # The failure is filter selection before rejection, not the 256*r+g math.
    a, b = (64/255, 0), (192/255, 0)
    decode = lambda p: 256*p[0]+p[1]
    mid = tuple((x+y)/2 for x, y in zip(a, b))
    assert decode(mid) == pytest.approx((decode(a)+decode(b))/2)


@pytest.mark.parametrize('size', [(2560, 1440), (1920, 1080)])
def test_sse_builder_center_and_scale_match_algebra(size):
    for z in (1, 17, 321):
        eye = (.123*z, -.217*z, z)
        rounded = m.project(eye, size, size, rounded=True)
        exact = m.project(eye, size, size)
        assert rounded == pytest.approx(exact, abs=2e-7)
    _, b, _, d = m.engine_coefficients(size, size)
    assert b*4096 == size[0]/2
    assert d*2048 == 2048-size[1]/2


def test_full_camera_clipped_to_reduced_view_is_a_different_unlogged_case():
    vp = (0, 360, 1920, 1080)
    p = (960.5, 900.5)
    reduced = m.pixel_to_texel(p, vp, shader_bias=False)
    cropped = m.pixel_to_texel(p, vp, shader_bias=False, full_view=(0, 0, 2560, 1440))
    assert reduced == pytest.approx((960.5, 1508.5))
    assert cropped == pytest.approx((640.5, 1688.5))
