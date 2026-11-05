#!/usr/bin/env python3
"""patch_audio_stereo.py -- sample-locked stereo for the AMIX audio driver,
applied after patch_audio_close.py to the build-time copies of audio.c and
audio.h.  Usage: patch_audio_stereo.py audio.c audio.h

The stock driver gives every open() one Paula channel.  A stereo player that
opens /dev/audio twice gets two channels that each start -- and restart after
any underrun -- on their own, so left and right drift apart by milliseconds:
an audible phasing sweep.  Commodore's code already carries two channel slots
per client (ports[2], ach[2]) but only ever uses the first.

New: ioctl AUDIO_STEREO (int 1/0, before the first write).  A stereo client
writes interleaved L,R signed bytes; it gets one left channel (0 or 3) and one
right channel (1 or 2).  Both are hand-fed together and started by ONE dmacon
write, run at the same period from equal-length buffers, and only the left
channel interrupts: each left interrupt services both.  So the pair cannot
drift.  audio_channel[left].partner = 1 + right while the pair (or its
silent tail) is live; a right channel that is a live partner is not free.

Also fixed (stock defect): when audiointr copied only part of a message into
a DMA buffer it did not advance b_rptr, so the next refill copied the same
bytes again (postaudio does advance).

Also fixed (stock defect): AUDIO_VOLUME and AUDIO_PERIOD answered the ioctl
and then "break" fell through to the common ACK path, which acknowledged the
same message again -- a second putnext() of a block already sent upstream, on
every ioctl.  Those cases now return.  Their own ACK never set ioc_error and
ioc_rval, which in an M_IOCDATA block overlay the copyresp's cp_pad1 and
cp_private (= 1): only the second ACK made the ioctl succeed.  They are set
now (without that, every AUDIO_PERIOD returned a garbage error).

The source is Commodore's and is not in this repository, and neither is
any of its text: every place is found by a fingerprint (linepatch.py),
and only our own lines are inserted; where the new code reuses stock
lines, they are copied from the input by their fingerprint.
"""
import sys

from linepatch import COPY, apply, read, write

# (what, lines, sha256 of those lines, at, lines deleted there, lines inserted)
PLACES_H = [
    ('struct audio_client', 3, '53b235a0a54e4c808fc6c0c3390c7367a5573c3f38e58b324547b4e278c97265', 0, 0, [
        '    int\t\t\tstereo;\t\t/* AUDIO_STEREO: ports[0] left, ports[1] right */',
    ]),
    ('struct audio_channel', 3, '2533df5e68254c66e86b81135428ede0ffddf533d739711b4d53856161cc282e', 0, 0, [
        '    int\t\t\t\tpartner; /* stereo left: 1 + index of its right channel */',
    ]),
    ('ioctl numbers', 2, '378b7841f6c554c9736bfe024a2757119e5eff08687baadc22b6edb2e4558a59', 0, 0, [
        '#define AUDIO_STEREO\t\t(AUIOC|3)\t/* int: 1 = interleaved L,R bytes */',
    ]),
]

PLACES_C = [
    ('declarations', 1, '9e582dbbea7630162ef287bbcc3f3fddf40f621de10944fca9c91616a056d635', 0, 0, [
        'static int chan_free(int);',
        'static void stop_channel(struct audio_channel *);',
    ]),
    ('declarations', 1, '0566248a3572dcc497f121a1770168249347ea1399fc6d05870b458524d338fa', 0, 1, [
        'static signed char audio_silent_buffer[4];\t/* 2 bytes per channel */',
    ]),
    ('audioopen', 1, '8307502cfa98dc21e87ac67b76ef4e4a35664390c157e8402aef8f5a8f0244fb', 0, 0, [
        '    acl->stereo = 0;',
    ]),
    ('audioclose', 1, '02102a4062a98b499c1d2a97059fe18ff09dbe692f5d455c0e2e33cfaf3d6cac', 0, 0, [
        '\t    audio_channel[i].partner = 0;',
    ]),
    ('audioioctl', 4, '249a5c48bb6b8ce2ab2712fd2f458c14885b0cc7f8601b5f0a852c79c93566b9', 0, 1, [
        '\t\t    return;',
    ]),
    ('audioioctl', 6, '916b9e61e8130c59d43865b0b97a61469ec02ed0398a43a4c64df6e4be6acc2e', 6, 0, [
        '\t\t/* the M_IOCDATA block still holds the copyresp: these overlay',
        '\t\t** cp_pad1/cp_private, so set them (the stock second ACK did) */',
        '\t\tiocbp->ioc_error = 0;',
        '\t\tiocbp->ioc_rval = 0;',
    ]),
    ('audioioctl', 4, '3554e39fe55afb38875bb7f14a7762df7e5dc01672423bffd0613c96052cf0e3', 0, 1, [
        '\t    return;\t/* answered above: the common ACK below would send it twice */',
    ]),
    ('audioioctl', 4, '6306c403de57b3353e9946f675517b824a7bf1b566bfab6a79f4799f93bc3beb', 0, 1, [
        '\t\t    return;',
    ]),
    ('audioioctl', 6, 'afbabecc9e36b78bc85e53c704277bb5b757acf75de640316ee6678286ebdf6c', 6, 0, [
        '\t\t/* the M_IOCDATA block still holds the copyresp: these overlay',
        '\t\t** cp_pad1/cp_private, so set them (the stock second ACK did) */',
        '\t\tiocbp->ioc_error = 0;',
        '\t\tiocbp->ioc_rval = 0;',
    ]),
    ('audioioctl', 4, 'cd093f691f5b181e8f45d8291e4068cdf221c2c3ab5876ae35832eb75f25f6f5', 0, 1, [
        '\t    return;\t/* answered above: the common ACK below would send it twice */',
        '\t}',
        '',
        '    case AUDIO_STEREO:',
        '\t{',
        COPY(7, '2885add841bde96231538d31d70264aaa2cbf2d91dcb0324cb63a5b3b2e194ee'),
        '\t\tcreq->cq_size = sizeof(int);',
        COPY(8, '4b0ca93d5c0205ff85aa56d1f046a4d4642d2aa0a451b6d5fd5eb986833bacdd'),
        '\t\tint s;',
        '',
        COPY(1, '5c3d0e11e7a888d858c9967bc1fbea234124f89094333200caeb3a99ecd10f2a'),
        '\t\ts = splaudio();',
        '\t\tif (!mp->b_cont || blklen(mp->b_cont) != sizeof(int) ||',
        '\t\t    (audio_client->flags & AUD_CATTACHED) || audio_client->mp ||',
        '\t\t    WR(q)->q_first)',
        '\t\t{',
        '\t\t    splx(s);',
        '\t\t    /* only before the first write */',
        COPY(4, 'dae61882359257c398db66be0117380299cdda38fb8043804e4713d6868743f5'),
        '\t\t    return;',
        '\t\t}',
        '\t\taudio_client->stereo = *(int *)mp->b_cont->b_rptr != 0;',
        '\t\tsplx(s);',
        '',
        COPY(1, '60ca766a305f43972d7de44fbb443895ff1b95e63b6ccd21836307d836f32f8c'),
        COPY(4, '60acb821b0933e31fea92c7d50857888c00c35c931d5e651e2c5031fa9bf9040'),
        '\t\tiocbp->ioc_error = 0;',
        '\t\tiocbp->ioc_rval = 0;',
        COPY(1, 'ad913a481b4cdb2ec91089d3491d0e5bb8a86716cce8c3482c37017bff15a05f'),
        '\t    }',
        '\t    return;',
    ]),
    ('audiointr', 1, 'bfaf123fb057360c789256349919d4abfab7c6091614cd95739610ebdc5eeb54', 0, 0, [
        '\t    struct audio_channel *pr;\t/* stereo: the right channel */',
        '\t    int k;',
    ]),
    ('audiointr', 2, '17ee0e9a6ba2a28dfc6a5630086be168f9450166830a79ee0e4e308daafaf5b8', 0, 0, [
        '',
        '\t    for (k = 0; k < MAXCHANNELS; ++k)',
        '\t\tif (audio_channel[k].partner == i + 1)',
        '\t\t    break;',
        '\t    if (k < MAXCHANNELS)',
        '\t\tcontinue;\t\t/* a right channel: its left one runs it */',
        '\t    pr = audio_channel[i].partner ? &audio_channel[audio_channel[i].partner - 1] : NULL;',
    ]),
    ('audiointr', 2, 'd643e6ef48c3d9e34c1d97e358834a745b47862b128c727fddf9aa5f0602829e', 0, 0, [
        '\t    if (pr && pr->pending > 0)',
        '\t\trelease_channel_buffer(pr);',
    ]),
    ('audiointr', 3, '350474191d685e61dc77fe61b028c601a5501e4ba2df7ce030f7e485197cff06', 0, 0, [
        '\t\t    else',
        '\t\t\tacl->mp->b_rptr += size;\t/* the rest next time, not all of it again */',
    ]),
    ('audiointr', 1, '0729a76d87aa32b826e3a4c7d68dc70e845189181ae5651202b1b524f07aabfa', 0, 0, [
        '\t\t\t    mp->b_rptr += size;',
    ]),
    ('audiointr', 1, '5bfcbccd680c340b7e170e183983c0e8e8b3e3bf556c8775aa977a5875f97b05', 0, 1, [
        '\t\t    fill_buffer(acl, audio_silent_buffer, acl->stereo ? 4 : 2);',
    ]),
    ('audiointr', 2, 'ecea575199aa92eaca839b75c832fb06028029e3b795e460b13d27dda3b44e48', 0, 0, [
        '\t\t    if (pr)',
        '\t\t\tpr->client = NULL;\t/* still reserved: partner stays set */',
    ]),
    ('audiointr', 2, '8444506e9da44f0428665e394e9e9a25977c77a4784a7536f6e96554a4eafb75', 0, 0, [
        '\t\t    if (pr)',
        '\t\t\tstop_channel(pr);',
        '\t\t    audio_channel[i].partner = 0;',
    ]),
    ('audiointr', 3, '37ae0eb55f4928e81d5a92fb8f8d35940787f7648592ceff343d02bb4ed2b4eb', 1, 0, [
        '\t\t    if (pr)',
        '\t\t\tstop_channel(pr);',
        '\t\t    audio_channel[i].partner = 0;',
    ]),
    ('postaudio', 2, 'd131624ebbf54e807901f13acb83d49615b6aaf4b701105c0229a2032f3c609e', 0, 0, [
        COPY(1, '4601898f4a6ff663c0d43fe6f9bf56a893c659900ed921f182a72a6f893f58e2'),
    ]),
    ('postaudio', 1, '4601898f4a6ff663c0d43fe6f9bf56a893c659900ed921f182a72a6f893f58e2', 0, 1, [
        '    if (acl->stereo)',
        '    {',
        '\tach[1] = &audio_channel[acl->ports[1]];',
        '\tach[1]->client = acl;',
        '\t/* Not (still) running as this pair, e.g. one half was idle or the',
        '\t** left one was last a mono channel: restart both from scratch. */',
        '\tif (ach[0]->partner != acl->ports[1] + 1 || ach[0]->pending != ach[1]->pending)',
        '\t{',
        '\t    stop_channel(ach[0]);',
        '\t    stop_channel(ach[1]);',
        '\t    ach[0]->partner = acl->ports[1] + 1;',
        '\t}',
        '    }',
        '    else if (ach[0]->partner)',
        '    {',
        '\t/* a left channel whose stereo tail is still playing: drop the tail */',
        '\tstop_channel(&audio_channel[ach[0]->partner - 1]);',
        '\tstop_channel(ach[0]);',
        '\tach[0]->partner = 0;',
        '    }',
    ]),
    ('postaudio', 3, '86b3f70f2b85216fdde96a431b1b5b185e3dc4fff55304fbc29fe7bc96beb5e6', 0, 1, [
        '',
        '/* silence a channel and forget its buffers (stereo pair (re)start, tails) */',
        COPY(1, '7c4b41d8a5ad04fa7cd17ef3516551f4dcbb5bbea1af670fe459bb279fab9172'),
        'stop_channel(struct audio_channel *ach)',
        '{',
        '    AMIGA->intena = AINTCLR | ach->dmadone;',
        '    AMIGA->dmacon = DMACLR | ach->enable;',
        '    AMIGA->intreq = AINTCLR | ach->dmadone;',
        '    ach->pending = 0;',
        '    ach->used = 0;',
        '    ach->nbytes[0] = ach->nbytes[1] = ach->nbytes[2] = 0;',
        '}',
        '',
    ]),
    ('fill_buffer', 2, '526c50879f7835483899f3e9f08a484541c6caf7b23d4964cef70fefbe05f03a', 0, 1, [
        '',
        '    if (acl->stereo)',
        '    {',
        '\t/* interleaved L,R: both channels take the same number of frames */',
        '\tsigned char *l, *r;',
        '\tint k;',
        '',
        '\tach[1] = &audio_channel[acl->ports[1]];',
        '\tsize = min(len / 2, ach[0]->size - ach[0]->nbytes[ach[0]->used]);',
        '\tsize = min(size, ach[1]->size - ach[1]->nbytes[ach[1]->used]);',
        '\tl = ach[0]->buffer[ach[0]->used] + ach[0]->nbytes[ach[0]->used];',
        '\tr = ach[1]->buffer[ach[1]->used] + ach[1]->nbytes[ach[1]->used];',
        '\tfor (k = 0; k < size; ++k)',
        '\t{',
        '\t    l[k] = data[2 * k];',
        '\t    r[k] = data[2 * k + 1];',
        '\t}',
        '\tach[0]->nbytes[ach[0]->used] += size;',
        '\tach[1]->nbytes[ach[1]->used] += size;',
        '\t/* a trailing odd byte is no frame: count it as taken */',
        '\treturn size == len / 2 ? len : 2 * size;',
        '    }',
        '',
    ]),
    ('fill_buffer', 3, '9e77cf263db9f0173f8c3025e4e50880460ae7f54e5a3bb5af235c60a6f3177c', 0, 1, [
        '',
        "/* free for a new client: nobody's, and not the right half of a live pair */",
        COPY(1, '817cce65ff7fa1bed2adedf1bb18dbfadfe616ed6a57920e305904bb1d21f60b'),
        'chan_free(int j)',
        '{',
        '    int k;',
        '',
        '    if (audio_channel[j].client != NULL)',
        '\treturn 0;',
        '    for (k = 0; k < MAXCHANNELS; ++k)',
        '\tif (audio_channel[k].partner == j + 1)',
        '\t    return 0;',
        '    return 1;',
        '}',
        '',
    ]),
    ('grab_channel', 1, '5301e14f6b5ff040aa46b23033a126139d687d1eed6562a18c7a79d8d9614528', 0, 0, [
        '\tif (acl->stereo)',
        '\t{',
        '\t    /* Paula wiring: channels 0 and 3 left, 1 and 2 right */',
        '\t    int l = acl->ports[0], r = acl->ports[1];',
        '',
        '\t    /* Our own pair, still playing out its tail after the queue ran dry',
        '\t    ** (audiointr detached us, as it does a mono client): take it back',
        '\t    ** as it is, so playback continues without a restart -- as a mono',
        '\t    ** client simply keeps its old port. */',
        '\t    if (!(audio_channel[l].client == NULL && audio_channel[r].client == NULL &&',
        '\t\t  audio_channel[l].partner == r + 1))',
        '\t    {',
        '\t\tl = chan_free(0) ? 0 : chan_free(3) ? 3 : -1;',
        '\t\tr = chan_free(1) ? 1 : chan_free(2) ? 2 : -1;',
        '\t\tif (l < 0 || r < 0)',
        '\t\t    return EBUSY;',
        '\t    }',
        '\t    acl->ports[0] = l;',
        '\t    acl->ports[1] = r;',
        '\t}',
    ]),
    ('grab_channel', 2, '3c387c2cf0135b88c70b0186a988958fb4b1e0cf1ba845d944ea689aecc2036e', 0, 2, [
        '\telse if (!chan_free(acl->ports[0]))',
    ]),
    ('grab_channel', 1, '8f4e6c8a36c5c5a8f2f764adc201c44adaeef5c8889245529b53f3e6b91081e1', 0, 1, [
        '\t\tif (chan_free(i)) /* is channel allocated? */',
    ]),
    ('grab_channel', 3, 'cd12b12df7ca3e3cbb27a1eda63960481acff722ba340650ab6bfb743af7ea64', 0, 1, [
        '',
    ]),
    ('allocate_channel_buffers', 1, 'eb8b521eb1104d50bad699f22fcc7f40b048e594f2c3ff2b983862ed87176369', 0, 1, [
        'allocate_one(struct audio_channel *ach)',
    ]),
    ('allocate_channel_buffers', 5, '8b15e059fd62892ac2e5a5e7e46c30f685221910eae98e319ef3e3b3b28e8c7f', 0, 5, [
        '    if (ach->size == 0)',
    ]),
    ('allocate_channel_buffers', 1, '2ed841d33ca64918cdd8bd168dd5c6375f9208e3d6060d353eab74d39e2f0271', 0, 1, [
        '\tif (!(ach->buffer[0] = AllocMem(AUDIO_BUFFER_SIZE, MEMF_CHIP)))',
    ]),
    ('allocate_channel_buffers', 1, 'f2869348816ad02ee39a41d799f9d70663f5f8f001aebc54888c72561049872e', 0, 1, [
        '\tif (!(ach->buffer[1] = AllocMem(AUDIO_BUFFER_SIZE, MEMF_CHIP)))',
    ]),
    ('allocate_channel_buffers', 2, 'cdc671f5aa6759936c5c4a8c503c29f19e4f43b28817aa292094444ebed2aa10', 0, 2, [
        '\t    FreeMem(ach->buffer[0], AUDIO_BUFFER_SIZE);',
    ]),
    ('allocate_channel_buffers', 1, '2f12d5a04a7e4a50119e1e6151c062e6c4b0010800bc61cc15a5c6a21e9968e3', 0, 1, [
        '\tif (!(ach->buffer[2] = AllocMem(AUDIO_BUFFER_SIZE, MEMF_CHIP)))',
    ]),
    ('allocate_channel_buffers', 3, 'a71befbc982f75eceb7de121fe3b85cd5f7c9fb59c822b86176d44665c1a3ec5', 0, 3, [
        '\t    FreeMem(ach->buffer[0], AUDIO_BUFFER_SIZE);',
        '\t    FreeMem(ach->buffer[1], AUDIO_BUFFER_SIZE);',
    ]),
    ('allocate_channel_buffers', 3, '8a5c796a0913c58946771aba9466626d40881ea78bf2a013418685e7783576ae', 0, 3, [
        '\tach->size = AUDIO_BUFFER_SIZE;',
        '\tach->nbytes[0] = ach->nbytes[1] = ach->nbytes[2] = 0;',
        COPY(1, 'fced987c873a03982969e19cbf37a828bd207f7a571e2eda171401b6529eddca'),
    ]),
    ('allocate_channel_buffers', 3, '49142cde1ac22603068cb75cbe0fe760144f4ce01c73f1cc1074c606dd4c62b6', 0, 1, [
        '',
        COPY(3, '44094686c8daf718a32060d54aa8974946642f40a488a7f197ba11343b718c2a'),
        '    if (allocate_one(&audio_channel[acl->ports[0]]))',
        COPY(1, 'f5dd4dc215a8b5eb91f8ad68f5ad1d2848e6a3c61fb11371fe12383ea3d9b261'),
        '    if (acl->stereo && allocate_one(&audio_channel[acl->ports[1]]))',
        COPY(1, 'f5dd4dc215a8b5eb91f8ad68f5ad1d2848e6a3c61fb11371fe12383ea3d9b261'),
        '    return 0;',
        '}',
        '',
    ]),
    ('handfeed', 3, '491e07ba560695a1127adfe0d327ca5597e83cf844e0e7e638086b06b10261b7', 3, 0, [
        '    int done, dma;',
    ]),
    ('handfeed', 2, 'c0305c0e98a102dfed252668870c8c1c7a4ff8b96da37f366c8d8f1a099ec38f', 0, 0, [
        '    ach[1] = acl->stereo ? &audio_channel[acl->ports[1]] : ach[0];',
        '    done = ach[0]->dmadone | ach[1]->dmadone;',
        '    dma = ach[0]->enable | ach[1]->enable;',
    ]),
    ('handfeed', 3, '92900b09af46a663273c8835db65a7ce821b376176bdc0240ddf013488d21d55', 0, 3, [
        '    AMIGA->intena = AINTCLR | done;\t/* disable interupts */',
        '    AMIGA->dmacon = DMACLR | dma;\t/* disable dma for now */',
        '    AMIGA->intreq = AINTCLR | done;\t/* clear interupt */',
    ]),
    ('handfeed', 1, '96623e236f910c2a9cbdbd5b8ac247511703aaa9b52c56dd2a549b0b8c203175', 0, 1, [
        '    ach[1]->audio->data = 0;\t\t/* until we can post the actual sound in */',
    ]),
    ('handfeed', 2, '0edaae25f1f1cf46515a72aad5251bbb1c3c9070d19b9dd1739499f28e3eb8c1', 0, 2, [
        '    while ((AMIGA->intreqr & done) != done)',
        '\t;\t\t\t\t/* busy wait for hand fed word(s) */',
    ]),
    ('handfeed', 3, '78fb9977d80f11207ec643f453028eeeb910432a16327f602e8facda79eec6e9', 0, 3, [
        '    AMIGA->dmacon = DMASET | dma;\t/* enable dma: both halves on one write */',
        '    AMIGA->intreq = AINTCLR | done;\t/* clear interupt */',
        '    AMIGA->intena = AINTSET | ach[0]->dmadone;\t/* only the left one interrupts */',
    ]),
    ('handfeed', 1, 'c2873c9fd8ba341db0a506987dbdbc71b1cb307cc7db750d0f3092525d34e82f', 1, 0, [
        '    ach[1]->audio->loud = acl->volume;',
        '    ach[1]->audio->period = acl->period;',
    ]),
    ('handfeed', 3, 'ea9ad5176c3a511aa3bea06cd075475ab211330681b16ce324f56ca121cca8ab', 0, 1, [
        '',
    ]),
    ('loadup_dma', 1, 'ded99142bee2141785f6c9beb50ef0a068d6c9f85cea3a6a6dac5b4c1550bfb9', 0, 1, [
        'loadup_one(struct audio_client *acl, struct audio_channel *ach, int flags)',
    ]),
    ('loadup_dma', 5, '4569d5f168b86721686f11ef6eeee4215e0412ffdfa53670625952ec4a928938', 0, 5, [
        '    if (ach->nbytes[ach->used] == 0)',
    ]),
    ('loadup_dma', 2, '5d1968b92e4a25e23b8efb8c76372c7504aaa3ec436df3b3447f559e9b5a1263', 0, 2, [
        '    ach->audio->buf = ach->buffer[ach->used];',
        '    ach->audio->size = ach->nbytes[ach->used] / 2;',
    ]),
    ('loadup_dma', 2, '2abeb320d5410688dab8af1d788d6464600120c28669d0db0929eabc0f531ddd', 0, 2, [
        '    if (ach->used == 2)',
        '\tach->used = 0;',
    ]),
    ('loadup_dma', 1, 'f4bc827bf06d6ea3e6dcc2ac56e30b8100e48c8189613a2992a3cf079baaafc2', 0, 1, [
        '\t++ach->used;',
    ]),
    ('loadup_dma', 2, 'ee59042a7024145af2bb97697f07482ef5d183b579b6df3119306b4cfb2ecdb5', 0, 2, [
        '\tach->audio->period = 1;',
        '\tach->audio->loud = 0;',
    ]),
    ('loadup_dma', 2, '2a61377064633f473c10be26b6e928e6222620f8e19b13c85eb8ad626f975441', 0, 2, [
        '\tach->audio->period = acl->period;',
        '\tach->audio->loud = acl->volume;',
    ]),
    ('loadup_dma', 1, '28ab52185861072615ef06f6279b74d090e003142b7169233d7544d5da5b3b47', 0, 1, [
        '    ++ach->pending;',
    ]),
    ('loadup_dma', 3, '38958b6748c4a6869400335f60b71bcb86495562fe1ebe72ce4e36e6a5e6c548', 0, 1, [
        '',
        COPY(3, 'c410f61328a79e2d21cad459dd1826a4853c1cd66f37c198fa3cf87ac6c9bfbd'),
        '    loadup_one(acl, &audio_channel[acl->ports[0]], flags);',
        '    if (acl->stereo)',
        '\tloadup_one(acl, &audio_channel[acl->ports[1]], flags);',
        '}',
        '',
    ]),
]


cpath, hpath = sys.argv[1], sys.argv[2]
c, h = read(cpath), read(hpath)
if any("AUDIO_STEREO" in l for l in h) or any("AUDIO_STEREO" in l for l in c):
    sys.exit("[FAIL] audio.c/audio.h already carry AUDIO_STEREO")
if not any("ISSUE-68" in l for l in c):
    sys.exit("[FAIL] audio.c lacks the ISSUE-68 close fix: patch_audio_close.py first")
c = apply("audio.c", c, PLACES_C)
h = apply("audio.h", h, PLACES_H)
write(cpath, c)
write(hpath, h)
print("      [ok] AUDIO_STEREO: locked L/R pair (0|3 + 1|2), one interrupt, one dmacon write")
print("      [ok] audiointr: partly copied messages advance (no replay)")
print("      [ok] ioctl: AUDIO_VOLUME/AUDIO_PERIOD acknowledged once, with ioc_error/ioc_rval set")
