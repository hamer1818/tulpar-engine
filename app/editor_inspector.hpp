// L6 APP — TulparEngine Modern Bileşen Müfettişi ve Kart Sistemi (editor_inspector.hpp)
//
// ESİNLENME VE MİMARİ KARŞILAŞTIRMA:
// 1. O3DE (Open 3D Engine) - ComponentEditorHeader & Card Actions
//    O3DE'de her bileşen bir "Card"dır. Başlıkta katlama (collapse), aktif/pasif anahtarı,
//    sağ tık veya "..." menüsünden "Reset to Default", "Copy Component", "Paste Values",
//    "Remove Component" eylemleri sunulur.
// 2. Prowl Game Engine - EditorGUI.Row & InspectorPanel
//    Prowl'un her bileşeni panoda izole eden, tek satırlık ve etiket-hizalı property grid
//    yapısı ile unapplied change prompt mekanizması esas alınmıştır.
// 3. EdenSpark / Dagor Engine - inGameEditor / daECS
//    Bileşen verisinin doğrudan ofset ve kopyalama ile güncellenmesi (cache locality).

#pragma once

#include <imgui.h>
#include <IconsMaterialDesign.h> // ikon makrolari (IconFontCppHeaders, Zlib)
#include "app/editor_widgets.hpp"
#include "content/scene.hpp"
#include "content/reflect.hpp"

namespace tulpar::engine::app {

// Pano (Clipboard): Bileşen değerlerini kopyalayıp başka bir varlığa yapıştırma
struct ComponentClipboard {
  content::SceneEntity snapshot;
  uint32_t component_bit = 0;
  bool has_data = false;
};

inline ComponentClipboard &get_component_clipboard() {
  static ComponentClipboard cb;
  return cb;
}

enum class ComponentCardAction : uint8_t {
  None = 0,
  Reset,
  Copy,
  Paste,
  Remove
};

// Bileşen Kart Başlığı: O3DE ve Prowl tarzı sağ tık bağlam menüsü desteği
inline ComponentCardAction draw_component_card_context_menu(const char *name, uint32_t component_bit) {
  ComponentCardAction action = ComponentCardAction::None;
  char popup_id[64];
  std::snprintf(popup_id, sizeof popup_id, "##ctx_%s", name);

  if (ImGui::BeginPopupContextItem(popup_id)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.65f, 0.95f, 1.0f));
    ImGui::TextDisabled("\xE2\x9A\x99  %s \xC4\xB0\xC5\x9ELEMLER\xC4\xB0", name); // ⚙
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (ImGui::MenuItem("\xE2\x86\xBA  Varsayılana Sıfırla")) { // ↺
      action = ComponentCardAction::Reset;
    }

    if (ImGui::MenuItem(ICON_MD_CONTENT_COPY "  Bileşeni Kopyala")) { // 📋
      action = ComponentCardAction::Copy;
    }

    const auto &cb = get_component_clipboard();
    const bool can_paste = cb.has_data && (cb.component_bit == component_bit);
    if (ImGui::MenuItem(ICON_MD_CONTENT_PASTE "  Bileşen Değerlerini Yapıştır", nullptr, false, can_paste)) { // 📥
      action = ComponentCardAction::Paste;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("\xE2\x9C\x95  Bileşeni Kaldır")) { // ✕
      action = ComponentCardAction::Remove;
    }

    ImGui::EndPopup();
  }
  return action;
}

// O3DE tarzı bileşen kartı başlık çizimi
inline bool begin_component_card(const char *icon, const char *name, uint32_t component_bit,
                                bool *enabled, ComponentCardAction *out_action,
                                bool default_open = true, Tone icon_tone = Tone::Accent) {
  bool remove_clicked = false;
  const bool open = component_header(icon, name, enabled, &remove_clicked, default_open, icon_tone);

  ComponentCardAction act = draw_component_card_context_menu(name, component_bit);
  if (remove_clicked) {
    act = ComponentCardAction::Remove;
  }
  if (out_action) {
    *out_action = act;
  }
  return open;
}

inline void end_component_card() {
  component_end();
}

template <typename CommitFn>
inline void process_component_card_action(ComponentCardAction act, uint32_t comp_bit,
                                         const content::SceneEntity &e, int entity_index,
                                         CommitFn &&commit_fn) {
  if (act == ComponentCardAction::Reset) {
    content::SceneEntity after = e;
    reflect::reset_component_to_defaults(after, comp_bit);
    commit_fn(entity_index, after);
  } else if (act == ComponentCardAction::Copy) {
    auto &cb = get_component_clipboard();
    cb.snapshot = e;
    cb.component_bit = comp_bit;
    cb.has_data = true;
  } else if (act == ComponentCardAction::Paste) {
    auto &cb = get_component_clipboard();
    if (cb.has_data && cb.component_bit == comp_bit) {
      content::SceneEntity after = e;
      reflect::copy_component_data(cb.snapshot, after, comp_bit);
      commit_fn(entity_index, after);
    }
  } else if (act == ComponentCardAction::Remove) {
    content::SceneEntity after = e;
    after.components &= ~comp_bit;
    commit_fn(entity_index, after);
  }
}

} // namespace tulpar::engine::app
