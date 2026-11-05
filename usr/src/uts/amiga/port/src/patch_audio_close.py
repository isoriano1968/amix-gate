#!/usr/bin/env python3
"""patch_audio_close.py -- ISSUE-68 fix, applied to a copy of the stock
usr/sys/amiga/driver/audio.c (AMIX 2.1c).  Usage: patch_audio_close.py audio.c

1. audioclose: a new body.  The stock close frees the client while
   audio_channel[].client may still point at it, so the next audio interrupt
   runs getq()/fill_buffer() on freed memory (a panic in audiointr).  The new
   close waits for the queue to drain (the interrupt wakes it via
   AUD_CCLOSING), then -- drained or interrupted by a signal -- silences and
   detaches every channel the client holds, frees the partial message, and
   unlinks it with a termination check, all at splaudio(), before kmem_free.
2. audiointr: when a waiting client's message is only partly posted, the
   stock code also stored it in the PREVIOUS list entry's mp; with two
   clients that frees it twice.  The rest stays in acl->next->mp.

The source is Commodore's and is not in this repository, and neither is
any of its text: every place is found by a fingerprint (linepatch.py),
and only our own lines are inserted.
"""
import sys

from linepatch import COPY, apply, read, write

# (what, lines, sha256 of those lines, at, lines deleted there, lines inserted)
PLACES = [
    ('audioclose', 2, '425ab30e339422f0b061435908d189f4e94ae421b3fc497f520b910956fefb06', 1, 1, [
        '    int s, i;',
    ]),
    ('audioclose', 3, 'af9b1ba5f303245575b228fd331a56dd9b68f7ce27a2af0d48a01cd1b4d36c8f', 0, 0, [
        '    /* AMIX 68040/68060 port, ISSUE-68: never free acl while an audio',
        '    ** channel can still reach it from the interrupt. */',
    ]),
    ('audioclose', 2, 'a03e382fe6840bc7810546c05aabad7dbb53acbdab3a800dc58f964f4e8faa42', 0, 2, [
        '    /* Let what is queued play out; audiointr detaches the client and',
        '    ** wakes us.  A signal (^C, kill) ends the wait. */',
        '    while (acl->mp != NULL || WR(q)->q_first != NULL || (acl->flags & AUD_CATTACHED))',
    ]),
    ('audioclose', 1, 'ceb280d2d9a43e6159f20e1a1e4b32d49f917ee8383b5e96d830234ebad8274c', 0, 1, [
        COPY(1, 'b0845f9027a01775654732318c2c2fd08403e2daf1348126da5e0fe3e76830a1'),
        '\tif (sleep((caddr_t)acl, PZERO+1|PCATCH))',
        '\t    break;',
        '    }',
        '    acl->flags &= ~AUD_CCLOSING;',
    ]),
    ('audioclose', 3, '23a8c419d2b6d67aaf0aee8d364fdd594950a5d8e33038e2e899b58de511ea6d', 0, 3, [
        "    /* Whatever is left: silence and detach this client's channels. */",
        '    for (i = 0; i < MAXCHANNELS; ++i)',
        '    {',
        '\tif (audio_channel[i].client == acl)',
    ]),
    ('audioclose', 6, '551d63c931639cd9a88760302ef89f5635e7063268fb3d41be8fc35e0bee5115', 0, 6, [
        '\t    AMIGA->intena = AINTCLR | audio_channel[i].dmadone;',
        '\t    AMIGA->dmacon = DMACLR | audio_channel[i].enable;',
        '\t    AMIGA->intreq = AINTCLR | audio_channel[i].dmadone;',
        '\t    audio_channel[i].client = NULL;',
        '\t    audio_channel[i].pending = 0;',
        '\t    audio_channel[i].used = 0;',
        '\t    audio_channel[i].nbytes[0] = audio_channel[i].nbytes[1] =',
        '\t\taudio_channel[i].nbytes[2] = 0;',
    ]),
    ('audioclose', 2, '8b63d4e2e6cce4baf5403993210754bed64f3d3fa8e57007f2c3eeff4491a972', 0, 1, [
        '    acl->flags &= ~AUD_CATTACHED;',
        COPY(2, 'e0626d406484ca33fa32e6a60a2e58159123026535799245ae1de0ada954e389'),
        COPY(3, '97d395401365084b589d638379cd3e6a0eb7224e8416136173b6c32c89240822'),
        '    acl->q = NULL;',
    ]),
    ('audioclose', 3, '8409f14f011c4fff5dcd05da14c83cec6688f105b11ea4a0314888596c271c74', 0, 3, [
        '    /* Unlink -- only if it is on the list. */',
        '    for (aclp = &audio_clients; aclp->next != NULL && aclp->next != acl; aclp = aclp->next)',
    ]),
    ('audioclose', 2, '207ca0b845eee6b055d5a19897b78a6ea2a5d96f3f1d5b263bb0d4e00fb69aae', 0, 2, [
        '    if (aclp->next == acl)',
        '\taclp->next = acl->next;',
    ]),
    ('audioclose', 3, '619147b961a4d57e3076c7c35ec5b5c3856fed5d41f47303cffbce2a3cbbce93', 0, 1, [
        '',
    ]),
    ('audiointr', 1, '3c6770c500cdff11cd23d4e78c710d322fa476e4a751f224e289e53749ddc838', 0, 1, [
        '\t\t    /* partly posted: the rest stays in acl->next->mp */',
    ]),
]


p = sys.argv[1]
lines = read(p)
if any("ISSUE-68" in l for l in lines):
    sys.exit("[FAIL] audio.c already carries the ISSUE-68 close fix")
write(p, apply("audio.c", lines, PLACES))
print("      [ok] audioclose: drain wait, channels detached, guarded unlink")
print("      [ok] audiointr: partly posted message no longer aliased")
