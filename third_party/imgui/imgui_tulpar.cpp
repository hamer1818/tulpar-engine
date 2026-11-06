// TULPAR YAMASI (imconfig.h) — ImGui test motoru kancalarinin tanimi. Vendored degil, bizim dosya:
// IMGUI_ENABLE_TEST_ENGINE acikken imgui.cpp bu dort sembolu ister; bunlar yalniz isaretciye aktarir
// (app/editor_surucu.cpp kurar). Pencere adi/kirpma icin cagiran GImGui->CurrentWindow'u okur.
#include "imgui.h"
#include "imgui_internal.h"

void (*GImGuiTulparOgeKanca)(unsigned, const float *, const char *, int) = nullptr;

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *) {
  if (!GImGuiTulparOgeKanca) return;
  const float d[4] = {bb.Min.x, bb.Min.y, bb.Max.x, bb.Max.y};
  GImGuiTulparOgeKanca(id, d, nullptr, 0);
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext *, ImGuiID id, const char *label, ImGuiItemStatusFlags flags) {
  if (GImGuiTulparOgeKanca) GImGuiTulparOgeKanca(id, nullptr, label ? label : "", (int)flags);
}
void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID) { return nullptr; }
