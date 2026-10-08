# Third-party notices

## Brotli (vendored)

`third_party/brotli/` contains the Brotli decoder sources used for WOFF2
decompression, taken unmodified from the
[Brotli](https://github.com/google/brotli) project.

```
Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors.
MIT License — see third_party/brotli/LICENSE
```

## fontTools (reference implementation, not included)

This project is an independent C++ reimplementation of the behaviour of the
Python [fontTools](https://github.com/fonttools/fonttools) toolkit. fontTools
is **not** vendored or linked: it is only the external reference the
implementation was developed and checked against.

```
Copyright (c) 2017 Just van Rossum and the fontTools developers.
Copyright (c) 2015 Google Inc.
MIT License — https://github.com/fonttools/fonttools/blob/main/LICENSE
```

## OpenType specification

Table layouts, the Type 2 charstring format, the WOFF/WOFF2 container formats
and the TrueType hinting model follow the public
[OpenType specification](https://learn.microsoft.com/typography/opentype/spec/)
and its Appendix documents (Microsoft, Adobe, Google, W3C).
