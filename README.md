# SoulseekQT Transfer Logging

SoulseekQt doesn't save its diagnostics logs anywhere, so you have to copy them out of the UI by hand. This is a tiny `LD_PRELOAD` hook that writes them to a file instead. Built for the [realies/soulseek](https://hub.docker.com/r/realies/soulseek) Docker image.

## Building

```
gcc -shared -fPIC -O2 -o slsklog.so slsklog.c -ldl -lpthread
```

No gcc? Build it with Docker instead:

```
docker run --rm -v "$PWD":/src -w /src gcc:12 gcc -shared -fPIC -O2 -o slsklog.so slsklog.c -ldl -lpthread
```

## Setup

Mount `slsklog.so` into the container and add these env vars:

```yaml
environment:
  LD_PRELOAD: /hook/slsklog.so
  SLSK_HOOK_LOG: /data/.SoulseekQt/transfers.log
  SLSK_HOOK_MAX_SIZE: 50M
volumes:
  - ./slsklog.so:/hook/slsklog.so:ro
```

Then turn on **Options → UI → Show diagnostics** in Soulseek. The client doesn't log anything with it off.

When the log hits `SLSK_HOOK_MAX_SIZE` it moves to `transfers.log.1` and starts fresh. Set it to `0` to never rotate.
