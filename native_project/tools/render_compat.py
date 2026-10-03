"""Experimental single-threaded renderer candidate for this exact Unity asset.

Do not enable by default until device rendering has been validated.
"""
import hashlib

def patch(original):
    if hashlib.sha256(original).hexdigest()!='3fa92efa91d5a1f3b5b73a1421168f048e9ba0b69d1a671c773f60bdeb589591':
        raise ValueError('Unknown globalgamemanagers; refusing render settings patch')
    out=bytearray(original)
    for offset in (428568,428569):
        assert out[offset]==1
        out[offset]=0
    return bytes(out)
