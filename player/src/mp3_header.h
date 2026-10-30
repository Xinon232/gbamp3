#pragma once
/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). */
struct MP3Header {unsigned bytes,rate,channels,samples,bitrate;};
int mp3_header(const unsigned char *p,struct MP3Header *h);
