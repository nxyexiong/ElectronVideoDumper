#pragma once

bool start_ffmpeg(PROCESS_INFORMATION& pi, HANDLE& ffmpeg_stdin_write, const std::wstring& cmd);
bool ffmpeg_write_frame(HANDLE pipe, const uint8_t* data, const size_t size);
