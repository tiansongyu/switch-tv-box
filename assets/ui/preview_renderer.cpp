// CPU preview of the real ImGui home draw lists, without a Switch or a GPU.
// Included after extracted production draw functions by scripts/preview_home.py.
#include <vector>

static void write_preview(const char *path, ImDrawData *data, const unsigned char *atlas, int tw, int th)
{
    constexpr int width = 1280, height = 720;
    std::vector<unsigned char> pixels(width * height * 3, 0);
    auto edge = [](ImVec2 a, ImVec2 b, float x, float y) {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    for (int n = 0; n < data->CmdListsCount; ++n)
    {
        const ImDrawList *list = data->CmdLists[n];
        for (const ImDrawCmd &command : list->CmdBuffer)
        {
            if (command.UserCallback) continue;
            for (unsigned index = 0; index < command.ElemCount; index += 3)
            {
                const ImDrawVert *v[3];
                for (int i = 0; i < 3; ++i)
                    v[i] = &list->VtxBuffer[command.VtxOffset + list->IdxBuffer[command.IdxOffset + index + i]];
                const float area = edge(v[0]->pos, v[1]->pos, v[2]->pos.x, v[2]->pos.y);
                if (fabsf(area) < 0.00001f) continue;
                const int left = std::max({0, (int)command.ClipRect.x, (int)floorf(std::min({v[0]->pos.x,v[1]->pos.x,v[2]->pos.x}))});
                const int top = std::max({0, (int)command.ClipRect.y, (int)floorf(std::min({v[0]->pos.y,v[1]->pos.y,v[2]->pos.y}))});
                const int right = std::min({width, (int)command.ClipRect.z, (int)ceilf(std::max({v[0]->pos.x,v[1]->pos.x,v[2]->pos.x}))});
                const int bottom = std::min({height, (int)command.ClipRect.w, (int)ceilf(std::max({v[0]->pos.y,v[1]->pos.y,v[2]->pos.y}))});
                for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x)
                {
                    const float w[3] = {
                        edge(v[1]->pos,v[2]->pos,x+0.5f,y+0.5f) / area,
                        edge(v[2]->pos,v[0]->pos,x+0.5f,y+0.5f) / area,
                        edge(v[0]->pos,v[1]->pos,x+0.5f,y+0.5f) / area
                    };
                    if (w[0] < -0.00001f || w[1] < -0.00001f || w[2] < -0.00001f) continue;
                    float u = 0, tex_v = 0, color[4] = {};
                    for (int i = 0; i < 3; ++i)
                    {
                        u += v[i]->uv.x * w[i];
                        tex_v += v[i]->uv.y * w[i];
                        for (int c = 0; c < 4; ++c) color[c] += ((v[i]->col >> (c * 8)) & 255) * w[i];
                    }
                    const float tx = std::max(0.0f, std::min((float)tw - 1, u * tw - 0.5f));
                    const float ty = std::max(0.0f, std::min((float)th - 1, tex_v * th - 0.5f));
                    const int x0 = (int)tx, y0 = (int)ty, x1 = std::min(x0 + 1, tw - 1), y1 = std::min(y0 + 1, th - 1);
                    const float fx = tx - x0, fy = ty - y0;
                    const float tex_alpha = atlas[y0*tw+x0]*(1-fx)*(1-fy) + atlas[y0*tw+x1]*fx*(1-fy) +
                                            atlas[y1*tw+x0]*(1-fx)*fy + atlas[y1*tw+x1]*fx*fy;
                    const float alpha = tex_alpha * color[3] / (255.0f * 255.0f);
                    const int pixel = (y * width + x) * 3;
                    for (int c = 0; c < 3; ++c)
                        pixels[pixel+c] = (unsigned char)std::max(0.0f, std::min(255.0f, color[c]*alpha + pixels[pixel+c]*(1-alpha)));
                }
            }
        }
    }
    FILE *output = fopen(path, "wb");
    if (!output) { perror(path); exit(1); }
    fprintf(output, "P6\n%d %d\n255\n", width, height);
    if (fwrite(pixels.data(), 1, pixels.size(), output) != pixels.size()) exit(1);
    fclose(output);
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    const char *scenario = argv[3];
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    ImFontConfig cfg;
    cfg.OversampleH = cfg.OversampleV = 1;
    if (!io.Fonts->AddFontFromFileTTF(argv[1], 22.0f, &cfg, io.Fonts->GetGlyphRangesChineseFull())) return 1;
    unsigned char *atlas;
    int tw, th;
    io.Fonts->GetTexDataAsAlpha8(&atlas, &tw, &th);
    io.Fonts->SetTexID((ImTextureID)1);
    home_ui_init(nullptr, strcmp(scenario, "english") != 0);
    PlayerHomeViewState home = {};
    home.storage_ready = home.video_ready = home.network_ready = true;
    home.dlna_running = home.airplay_running = true;
    home.iptv_channel_count = 128;
    home.iptv_source_count = 3;
    snprintf(home.ip_address, sizeof(home.ip_address), "192.168.1.108");
    snprintf(home.iptv_last_name, sizeof(home.iptv_last_name), "%s", home_ui_text("CCTV 9 · Documentary", "CCTV 9 · 纪录频道"));
    if (!strcmp(scenario, "playing")) home.playback_active = home.iptv_playback_active = home.player_focused = true;
    if (!strcmp(scenario, "pairing"))
    {
        home.airplay_pin_visible = true;
        snprintf(home.airplay_pin, sizeof(home.airplay_pin), "4831");
    }
    if (!strcmp(scenario, "offline"))
    {
        home.network_ready = home.airplay_running = home.dlna_running = false;
        home.iptv_channel_count = home.iptv_source_count = 0;
        home.iptv_last_name[0] = 0;
        home.has_error = true;
        snprintf(home.error_line, sizeof(home.error_line), "网络未连接，请在系统设置中连接 Wi-Fi 后重新启动。");
    }
    if (!strcmp(scenario, "long-title"))
    {
        home.playback_active = home.iptv_playback_active = true;
        snprintf(home.iptv_last_name, sizeof(home.iptv_last_name), "超长频道名称测试：纪录片 / 旅行 / 城市风景 / 音乐现场 / 无损画质 / 周末精选节目");
    }
    ImGui::NewFrame();
    draw_home_screen(ImGui::GetBackgroundDrawList(), home, 1280, 720);
    ImGui::Render();
    write_preview(argv[2], ImGui::GetDrawData(), atlas, tw, th);
    ImGui::DestroyContext();
    return 0;
}
