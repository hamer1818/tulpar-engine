// L6 APP — Hızlı Komut ve Varlık Paleti (editor_palette.hpp)
//
// ESİNLENME VE MİMARİ:
// 1. Sublime Text / VS Code / Unreal Engine Command Palette:
//    Ctrl+P veya Ctrl+K ile açılan, tüm editör komutlarını, sahne varlıklarını
//    ve proje kaynaklarını klavyeden el çekmeden anında arayıp çalıştıran arayüz.
// 2. Bulanık Arama (Fuzzy Matching):
//    Saf C++ alt-dize ve karakter sırası puanlaması (zero-alloc).
//    Karakter atlamalı arama ("skk" -> "SokakLambasi", "kyd" -> "Kaydet").
//
// SIFIR TAHSİS SÖZLEŞMESİ:
// - STL kullanılmaz; sabit boyutlu sonuç listeleri (kMaxPaletteResults = 32).
// - Kare başına dinamik heap tahsisi yapılmaz.

#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <imgui.h>

#include "app/editor_commands.hpp"
#include "content/scene.hpp"

namespace tulpar::engine::app {

constexpr uint32_t kMaxPaletteResults = 48;
constexpr uint32_t kPaletteQueryLen = 64;

enum class PaletteItemType : uint8_t {
  Command,
  Entity,
  Action
};

struct PaletteItem {
  PaletteItemType type = PaletteItemType::Command;
  char title[64] = {0};
  char subtitle[128] = {0};
  char shortcut[32] = {0};
  uint32_t data_id = 0; // CommandId veya Entity Index
  int score = 0;
};

struct PaletteState {
  bool is_open = false;
  bool request_focus = false;
  char query[kPaletteQueryLen] = {0};
  int selected_index = 0;
};

// =============================================================================
// SAF C++ BULANIK ARAMA (FUZZY MATCH) MOTORU
// =============================================================================

inline char to_lower_char(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

inline bool fuzzy_match(const char *pattern, const char *haystack, int &score) {
  if (!pattern || !pattern[0]) {
    score = 0;
    return true; // Boş sorgu her şeyi eşler
  }
  if (!haystack || !haystack[0]) return false;

  int p_len = static_cast<int>(std::strlen(pattern));
  int h_len = static_cast<int>(std::strlen(haystack));

  int p_idx = 0;
  int h_idx = 0;
  int match_score = 0;
  int consecutive = 0;
  bool prev_is_sep = true;

  while (p_idx < p_len && h_idx < h_len) {
    char pc = to_lower_char(pattern[p_idx]);
    char hc = to_lower_char(haystack[h_idx]);

    if (pc == hc) {
      match_score += 10;
      if (consecutive > 0) match_score += consecutive * 5;
      if (prev_is_sep) match_score += 20; // Kelime başı eşleşme bonusu
      consecutive++;
      p_idx++;
    } else {
      consecutive = 0;
      match_score -= 1; // Boşluk cezası
    }

    prev_is_sep = (haystack[h_idx] == ' ' || haystack[h_idx] == '_' || haystack[h_idx] == '-' || haystack[h_idx] == ':');
    h_idx++;
  }

  if (p_idx == p_len) {
    score = match_score;
    return true;
  }
  return false;
}

// =============================================================================
// KOMUT PALETİ ARAYÜZ ÇİZİMİ
// =============================================================================

inline void palette_open(PaletteState &state) {
  state.is_open = true;
  state.request_focus = true;
  state.query[0] = 0;
  state.selected_index = 0;
}

inline void palette_toggle(PaletteState &state) {
  if (state.is_open) {
    state.is_open = false;
  } else {
    palette_open(state);
  }
}

inline void draw_command_palette(PaletteState &state, CommandTable &cmds, content::SceneDesc &scene, void *editor_state_ptr) {
  if (!state.is_open) return;

  ImGui::OpenPopup("Komut Paleti##Modal");

  // Ekranın üst-ortasında şık ve odaklanmış modal pencere
  ImGuiIO &io = ImGui::GetIO();
  float modal_w = 640.0f;
  float modal_h = 380.0f;
  ImGui::SetNextWindowSize(ImVec2(modal_w, modal_h), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - modal_w) * 0.5f, io.DisplaySize.y * 0.15f), ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.16f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.55f, 0.95f, 0.6f));

  if (ImGui::BeginPopupModal("Komut Paleti##Modal", &state.is_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

    // 1. Arama Girdisi
    ImGui::PushItemWidth(-1);
    if (state.request_focus) {
      ImGui::SetKeyboardFocusHere();
      state.request_focus = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    ImGui::InputTextWithHint("##PaletteQuery", "Komut, varlik veya islem arayin... (Esc: Kapat)", state.query, sizeof(state.query));
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopItemWidth();

    ImGui::Separator();
    ImGui::Spacing();

    // 2. Adayları Topla ve Puanla
    PaletteItem items[kMaxPaletteResults];
    uint32_t item_count = 0;

    // A) Editör Komutları
    const CommandDesc *all_cmds = cmds.all();
    for (uint32_t i = 0; i < CommandTable::count() && item_count < kMaxPaletteResults; i++) {
      const CommandDesc &cmd = all_cmds[i];
      if (cmd.id == CommandId::None) continue;

      int score = 0;
      char search_key[128];
      std::snprintf(search_key, sizeof(search_key), "%s %s %s",
                    command_category_name(cmd.category), cmd.name, cmd.key);

      if (fuzzy_match(state.query, search_key, score)) {
        PaletteItem &item = items[item_count++];
        item.type = PaletteItemType::Command;
        std::snprintf(item.title, sizeof(item.title), "%s", cmd.name);
        std::snprintf(item.subtitle, sizeof(item.subtitle), "[%s] %s",
                      command_category_name(cmd.category), cmd.help ? cmd.help : "");
        command_shortcut_text(cmd.shortcut, item.shortcut, sizeof(item.shortcut));
        item.data_id = static_cast<uint32_t>(cmd.id);
        item.score = score;
      }
    }

    // B) Sahne Varlıkları
    for (uint32_t i = 0; i < scene.entity_count && item_count < kMaxPaletteResults; i++) {
      const content::SceneEntity &ent = scene.entities[i];
      int score = 0;
      if (fuzzy_match(state.query, ent.name, score)) {
        PaletteItem &item = items[item_count++];
        item.type = PaletteItemType::Entity;
        std::snprintf(item.title, sizeof(item.title), "%s", ent.name);
        std::snprintf(item.subtitle, sizeof(item.subtitle), "[Varlik #%u] Konum (%.1f, %.1f, %.1f)",
                      i, ent.pos.x, ent.pos.y, ent.pos.z);
        item.shortcut[0] = 0;
        item.data_id = i;
        item.score = score - 5; // Komutlara hafif öncelik
      }
    }

    // Basit Ekleme Sıralaması (Skora göre azalan sıralama)
    for (uint32_t i = 0; i < item_count; i++) {
      for (uint32_t j = i + 1; j < item_count; j++) {
        if (items[j].score > items[i].score) {
          PaletteItem temp = items[i];
          items[i] = items[j];
          items[j] = temp;
        }
      }
    }

    // Klavye Gezintisi (Yukarı/Aşağı/Enter/Escape)
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
      state.selected_index++;
      if (state.selected_index >= static_cast<int>(item_count)) {
        state.selected_index = 0;
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      state.selected_index--;
      if (state.selected_index < 0) {
        state.selected_index = item_count > 0 ? static_cast<int>(item_count - 1) : 0;
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      state.is_open = false;
      ImGui::CloseCurrentPopup();
    }

    // 3. Sonuç Listesi
    if (ImGui::BeginChild("##PaletteResults", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
      if (item_count == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("  Eslesen sonuc bulunamadi.");
      } else {
        if (state.selected_index >= static_cast<int>(item_count)) {
          state.selected_index = static_cast<int>(item_count - 1);
        }

        bool execute_now = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        uint32_t chosen_idx = 0xFFFFFFFFu;

        for (uint32_t i = 0; i < item_count; i++) {
          const PaletteItem &item = items[i];
          bool is_selected = (static_cast<int>(i) == state.selected_index);

          ImGui::PushID(static_cast<int>(i));

          if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.45f, 0.85f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.52f, 0.95f, 0.7f));
          }

          char label[128];
          std::snprintf(label, sizeof(label), "%s##item_%u", item.title, i);

          if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 36.0f))) {
            chosen_idx = i;
          }

          if (is_selected) {
            ImGui::PopStyleColor(2);
            if (ImGui::IsWindowAppearing()) {
              ImGui::SetScrollHereY();
            }
          }

          // Sağda Kısayol, altta açıklama
          ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
          if (item.shortcut[0]) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 0.8f), "%s", item.shortcut);
          }

          ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 16.0f);
          ImGui::Indent(12.0f);
          ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "%s", item.subtitle);
          ImGui::Unindent(12.0f);

          ImGui::PopID();
        }

        if (execute_now && state.selected_index >= 0 && state.selected_index < static_cast<int>(item_count)) {
          chosen_idx = static_cast<uint32_t>(state.selected_index);
        }

        if (chosen_idx != 0xFFFFFFFFu && chosen_idx < item_count) {
          const PaletteItem &chosen = items[chosen_idx];
          if (chosen.type == PaletteItemType::Command) {
            cmds.invoke(static_cast<CommandId>(chosen.data_id));
          } else if (chosen.type == PaletteItemType::Entity) {
            // Seçimi güncelle (EditorState::sel)
            struct MinimalState {
              struct {
                int32_t primary = -1;
                uint32_t count = 0;
                void clear() { primary = -1; count = 0; }
                void toggle(int32_t id) { primary = id; count = 1; }
              } sel;
            };
            if (editor_state_ptr) {
              auto *st = reinterpret_cast<MinimalState*>(editor_state_ptr);
              st->sel.clear();
              st->sel.toggle(static_cast<int32_t>(chosen.data_id));
            }
          }
          state.is_open = false;
          ImGui::CloseCurrentPopup();
        }
      }
    }
    ImGui::EndChild();

    ImGui::EndPopup();
  }

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

} // namespace tulpar::engine::app
