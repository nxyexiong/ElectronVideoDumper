# Overview

A tool to dump encrypted videos from electron based apps.

# How does it work

- Hooks into DecryptingVideoDecoderDeliverFrame to get the decrypted video frames
- Hooks into FFmpegAudioDecoderOnNewFrame to get the decrypted audio frames
- Uses ffmpeg to store the decrypted frames into a single video file
