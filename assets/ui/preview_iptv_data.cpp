// Deterministic sample metadata for previewing the production IPTV draw lists.
static constexpr float kPi = 3.14159265358979323846f;
static int preview_filter;
static int preview_channels = 20;
static ImTextureID channel_logo_texture(const IptvChannel &) { return 0; }
int iptv_get_filter_index(void) { return preview_filter; }
uint32_t iptv_get_source_filter(void) { return 0; }
uint32_t iptv_get_playing_channel_id(void) { return 4; }
bool iptv_get_channel(int index, IptvChannel *out)
{
    if (index < 0 || index >= preview_channels) return false;
    *out = {};
    out->id = index + 1;
    out->favorite = preview_filter == 1 || index % 3 == 0;
    snprintf(out->name, sizeof(out->name), "CCTV %d · %s", index + 1, index % 2 ? "纪录频道" : "综合频道");
    snprintf(out->group, sizeof(out->group), "央视频道");
    snprintf(out->source, sizeof(out->source), "我的直播源");
    snprintf(out->now_title, sizeof(out->now_title), "%s", index % 2 ? "自然的故事" : "今日新闻");
    snprintf(out->next_title, sizeof(out->next_title), "接下来：城市与生活");
    out->now_start = time(nullptr) - 900;
    out->now_stop = time(nullptr) + 1800;
    return true;
}
bool iptv_get_source(int index, IptvSource *out)
{
    *out = {};
    out->id = index + 1;
    out->channel_count = 20;
    snprintf(out->name, sizeof(out->name), "直播源 %d", index + 1);
    return true;
}
bool iptv_get_filter(int index, char *name, size_t size, int *count)
{
    snprintf(name, size, "%s", index == 0 ? "全部频道" : index == 1 ? "收藏" : index == 2 ? "最近观看" : "央视频道");
    if (count) *count = preview_channels;
    return true;
}
