hdr2rgbm
========

This tool allows to convert Radiance HDR (.hdr) files to RGBM-encoded .tga files.
Conversion process is executed on the GPU, with use of DX11.

## RGBM

The idea is to use RGB and a multiplier in alpha. The thing is luminance isn't only in alpha. What is essentially stored in alpha is a range value. The remainder of the luminance is stored with the chrominance in rgb.

```
float3 rgbm_decode(float4 rgbm) {
  return 6.0 * rgbm.rgb * rgbm.a;
}
```

More readings - http://graphicrants.blogspot.com/2009/04/rgbm-color-encoding.html.
