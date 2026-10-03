"""Version-checked ARM64 patches requested for this exact Unity build."""
import struct
UNITY_HOOK_PATCHES = [
    (0x1A8944, '6B420CB9', '742E46F9'),
    (0x1A8ABC, '75BA0FF9', '68BA0FF9'),
    (0x1A8AC0, '75AA0FF9', '68AA0FF9'),
    (0x1A8AC4, '75920FF9', '68920FF9'),
    (0x1A8ACC, '75A20FF9', '68A20FF9'),
    (0x1A8AD0, '759A0FF9', '689A0FF9'),
    (0x1A8AD4, '758A0FF9', '688A0FF9'),
    (0x1A8AD8, '74B210F9', '68B210F9'),
    (0x1A8ADC, '76FA10F9', '68FA10F9'),
    (0x1A8AE0, '76F210F9', '68F210F9'),
    (0x1A8AE4, '76EA10F9', '68EA10F9'),
]
def patch_unity(original, include_user=False):
    patches = [('offline_reachability', 0x145FB4, bytes.fromhex('F44FBEA9FD7B01A9'), struct.pack('<II', 0x52800040, 0xD65F03C0))]
    if include_user:
        patches += [('P'+str(i), offset, bytes.fromhex(old), bytes.fromhex(new)) for i,(offset,old,new) in enumerate(UNITY_HOOK_PATCHES,1)]
    for name,offset,old,new in patches:
        actual = original[offset:offset+len(old)]
        if actual not in (old,new):
            raise ValueError('%s mismatch at 0x%X: expected %s, actual %s' % (name,offset,old.hex(),actual.hex()))
    patched = bytearray(original)
    records = []
    for name,offset,old,new in patches:
        patched[offset:offset+len(new)] = new
        records.append({'name':name,'library':'lib/arm64-v8a/libunity.so','file_offset':hex(offset),'expected':old.hex(),'replacement':new.hex()})
    assert len(patched)==len(original)
    assert all(patched[offset:offset+len(new)]==new for _,offset,_,new in patches)
    allowed = {offset+i for _,offset,_,new in patches for i in range(len(new))}
    assert all(i in allowed for i,(old,new) in enumerate(zip(original,patched)) if old!=new)
    return bytes(patched),records
