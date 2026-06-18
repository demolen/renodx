/*
 * Copyright (C) 2024 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define DEBUG_LEVEL_0

#include <sstream>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/random.hpp"
#include "../../utils/resource.hpp"
#include "../../utils/settings.hpp"
#include "./shared.h"

namespace {

renodx::mods::shader::CustomShaders custom_shaders = {
    // CustomShaderEntry(0x7EABC435),  // PS_BinkNoAlpha
    CustomShaderEntry(0xC44FC390),  // PS_BlitCalibration
    // CustomShaderEntry(0x9A3213A0),  // PS_LensDistortion
    CustomShaderEntry(0x3A668290),  // PS_Sharpen
    CustomShaderEntry(0x3025A695),  // PS_corona

    CustomShaderEntry(0xED97A548),  // PS_CompositeExtraFX
    CustomShaderEntry(0x561305D2),  // PS_CompositeHHExtraFX
    CustomShaderEntry(0xD0746A48),  // PS_CompositeHighDOFExtraFX
    CustomShaderEntry(0xA695CEB0),  // PS_CompositeHighDOFHHExtraFX
    // CustomShaderEntry(0x40497C42),  // PS_CompositeHighDOFNV
    CustomShaderEntry(0x01C9FD7F),  // PS_CompositeMBExtraFX
    CustomShaderEntry(0xA9CCF727),  // PS_CompositeMBHHExtraFX
    CustomShaderEntry(0xB3DDE381),  // PS_CompositeMBHighDOFExtraFX
    CustomShaderEntry(0xE83B0B7E),  // PS_CompositeMBHighDOFHHExtraFX
    // CustomShaderEntry(0x7DCEAC64),  // PS_CompositeMBHighDOFNV
    CustomShaderEntry(0xA7D7E765),  // PS_CompositeMBShallowHighDOFExtraFX
    CustomShaderEntry(0x88D3E3C8),  // PS_CompositeMBShallowHighDOFHHExtraFX
    // CustomShaderEntry(0xDDB307E0),  // PS_CompositeNV
    // CustomShaderEntry(0x864A867F),  // PS_CompositeSeeThrough
    CustomShaderEntry(0xD0EB7F86),  // PS_CompositeShallowHighDOFExtraFX
    CustomShaderEntry(0x2ACFAE90),  // PS_CompositeShallowHighDOFHHExtraFX

    CustomShaderEntry(0x4BA8AA92),  // PS_puddleMaskAndPassCombined
    CustomShaderEntry(0x06E3253E),  // PS_Rain
};

ShaderInjectData shader_injection;

uint32_t rain_descriptor_debug_count = 0;
uint32_t rain_descriptor_debug_suppressed_count = 0;

bool IsRainTextureBinding(uint32_t binding) {
  return binding >= 15u && binding <= 17u;
}

void LogRainDescriptorView(
    const char* source,
    uint32_t binding,
    reshade::api::descriptor_type type,
    reshade::api::resource_view view) {
  if (!IsRainTextureBinding(binding)) return;
  if (view.handle == 0u) return;

  rain_descriptor_debug_count++;
  if (rain_descriptor_debug_count > 240u) {
    rain_descriptor_debug_suppressed_count++;
    if (rain_descriptor_debug_suppressed_count == 1u) {
      reshade::log::message(
          reshade::log::level::info,
          "GTAV rain descriptor debug: suppressing additional t15-t17 descriptor logs after 240 entries.");
    }
    return;
  }

  std::stringstream s;
  s << "GTAV rain descriptor debug";
  s << " source=" << source;
  s << " binding=" << binding;
  s << " type=" << type;
  s << " view=0x" << std::hex << view.handle << std::dec;

  auto* view_info = renodx::utils::resource::GetResourceViewInfo(view);
  if (view_info == nullptr) {
    s << " view_info=false";
    reshade::log::message(reshade::log::level::info, s.str().c_str());
    return;
  }

  s << " view_format=" << view_info->desc.format;
  s << " view_usage=0x" << std::hex << static_cast<uint32_t>(view_info->usage) << std::dec;
  if (view_info->resource_info == nullptr) {
    s << " resource_info=false";
    reshade::log::message(reshade::log::level::info, s.str().c_str());
    return;
  }

  const auto& resource_info = *view_info->resource_info;
  const auto& desc = resource_info.desc;
  s << " resource=0x" << std::hex << resource_info.resource.handle << std::dec;
  s << " size=" << desc.texture.width << "x" << desc.texture.height;
  s << " format=" << desc.texture.format;
  s << " usage=0x" << std::hex << static_cast<uint32_t>(desc.usage) << std::dec;
  s << " state=0x" << std::hex << static_cast<uint32_t>(resource_info.initial_state) << std::dec;
  s << " clone_enabled=" << (resource_info.clone_enabled ? "true" : "false");
  s << " clone_target=" << (resource_info.clone_target != nullptr ? "true" : "false");
  if (resource_info.clone.handle != 0u) {
    s << " clone=0x" << std::hex << resource_info.clone.handle << std::dec;
    s << " clone_size=" << resource_info.clone_desc.texture.width << "x" << resource_info.clone_desc.texture.height;
    s << " clone_format=" << resource_info.clone_desc.texture.format;
  }

  reshade::log::message(reshade::log::level::info, s.str().c_str());
}

bool OnUpdateDescriptorTables(
    reshade::api::device*,
    uint32_t count,
    const reshade::api::descriptor_table_update* updates) {
  for (uint32_t update_index = 0; update_index < count; ++update_index) {
    const auto& update = updates[update_index];
    for (uint32_t i = 0; i < update.count; ++i) {
      const uint32_t binding = update.binding + i;
      if (!IsRainTextureBinding(binding)) continue;

      switch (update.type) {
        case reshade::api::descriptor_type::sampler_with_resource_view: {
          const auto& item = static_cast<const reshade::api::sampler_with_resource_view*>(update.descriptors)[i];
          LogRainDescriptorView("update_descriptor_tables", binding, update.type, item.view);
          break;
        }
        case reshade::api::descriptor_type::texture_shader_resource_view:
        case reshade::api::descriptor_type::texture_unordered_access_view:
        case reshade::api::descriptor_type::buffer_shader_resource_view:
        case reshade::api::descriptor_type::buffer_unordered_access_view: {
          const auto& view = static_cast<const reshade::api::resource_view*>(update.descriptors)[i];
          LogRainDescriptorView("update_descriptor_tables", binding, update.type, view);
          break;
        }
        default:
          break;
      }
    }
  }
  return false;
}

void OnPushDescriptors(
    reshade::api::command_list*,
    reshade::api::shader_stage,
    reshade::api::pipeline_layout,
    uint32_t,
    const reshade::api::descriptor_table_update& update) {
  for (uint32_t i = 0; i < update.count; ++i) {
    const uint32_t binding = update.binding + i;
    if (!IsRainTextureBinding(binding)) continue;

    switch (update.type) {
      case reshade::api::descriptor_type::sampler_with_resource_view: {
        const auto& item = static_cast<const reshade::api::sampler_with_resource_view*>(update.descriptors)[i];
        LogRainDescriptorView("push_descriptors", binding, update.type, item.view);
        break;
      }
      case reshade::api::descriptor_type::texture_shader_resource_view:
      case reshade::api::descriptor_type::texture_unordered_access_view:
      case reshade::api::descriptor_type::buffer_shader_resource_view:
      case reshade::api::descriptor_type::buffer_unordered_access_view: {
        const auto& view = static_cast<const reshade::api::resource_view*>(update.descriptors)[i];
        LogRainDescriptorView("push_descriptors", binding, update.type, view);
        break;
      }
      default:
        break;
    }
  }
}

float current_settings_mode = 0;

renodx::utils::settings::Setting* CreateDefault50PercentSetting(const renodx::utils::settings::Setting& setting) {
  auto* new_setting = new renodx::utils::settings::Setting(setting);
  new_setting->default_value = 50.f;
  new_setting->parse = [](float value) { return value * 0.02f; };
  return new_setting;
}

renodx::utils::settings::Setting* CreateDefault100PercentSetting(const renodx::utils::settings::Setting& setting) {
  auto* new_setting = new renodx::utils::settings::Setting(setting);
  new_setting->default_value = 100.f;
  new_setting->parse = [](float value) { return value * 0.01f; };
  return new_setting;
}

renodx::utils::settings::Setting* CreateMultiLabelSetting(const renodx::utils::settings::Setting& setting) {
  auto* new_setting = new renodx::utils::settings::Setting(setting);
  new_setting->value_type = renodx::utils::settings::SettingValueType::INTEGER;
  return new_setting;
}

renodx::utils::settings::Settings settings = {
    new renodx::utils::settings::Setting{
        .key = "SettingsMode",
        .binding = &current_settings_mode,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .can_reset = false,
        .label = "Settings Mode",
        .labels = {"Simple", "Intermediate", "Advanced"},
        .is_global = true,
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapType",
        .binding = &shader_injection.tone_map_type,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .can_reset = true,
        .label = "Tone Mapper",
        .section = "Tone Mapping",
        .tooltip = "Sets the tone mapper type",
        .labels = {"Vanilla", "RenoDRT"},
        .parse = [](float value) { return value * 3.f; },
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapPeakNits",
        .binding = &shader_injection.peak_white_nits,
        .default_value = 1000.f,
        .can_reset = false,
        .label = "Peak Brightness",
        .section = "Tone Mapping",
        .tooltip = "Sets the value of peak white in nits",
        .min = 48.f,
        .max = 4000.f,
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapGameNits",
        .binding = &shader_injection.diffuse_white_nits,
        .default_value = 203.f,
        .label = "Game Brightness",
        .section = "Tone Mapping",
        .tooltip = "Sets the value of 100% white in nits",
        .min = 48.f,
        .max = 500.f,
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapUINits",
        .binding = &shader_injection.graphics_white_nits,
        .default_value = 203.f,
        .label = "UI Brightness",
        .section = "Tone Mapping",
        .tooltip = "Sets the brightness of UI and HUD elements in nits",
        .min = 48.f,
        .max = 500.f,
    },
    new renodx::utils::settings::Setting{
        .key = "GammaCorrection",
        .binding = &shader_injection.gamma_correction,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Gamma Correction",
        .section = "Tone Mapping",
        .tooltip = "Emulates a display EOTF.",
        .labels = {"Off", "2.2", "BT.1886"},
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapHueProcessor",
        .binding = &shader_injection.tone_map_hue_processor,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .label = "Hue Processor",
        .section = "Tone Mapping",
        .tooltip = "Selects hue processor",
        .labels = {"OKLab", "ICtCp", "darkTable UCS"},
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .is_visible = []() { return current_settings_mode >= 2; },
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapWhiteClip",
        .binding = &shader_injection.tone_map_white_clip,
        .default_value = 100.f,
        .label = "White Clip",
        .section = "Tone Mapping",
        .min = 0.f,
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .is_visible = []() { return current_settings_mode >= 2; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeStrength",
        .binding = &shader_injection.color_grade_strength,
        .default_value = 100.f,
        .label = "Strength",
        .section = "Scene Grading",
        .tooltip = "Scene grading as applied by the game",
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .parse = [](float value) { return value * 0.01f; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeHueCorrection",
        .binding = &shader_injection.color_grade_hue_correction,
        .default_value = 100.f,
        .label = "Hue Correction",
        .section = "Scene Grading",
        .tooltip = "Corrects per-channel hue shifts from per-channel grading.",
        .min = 0.f,
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .parse = [](float value) { return value * 0.01f; },
        .is_visible = []() { return current_settings_mode >= 2; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeSaturationCorrection",
        .binding = &shader_injection.color_grade_saturation_correction,
        .default_value = 100.f,
        .label = "Saturation Correction",
        .section = "Scene Grading",
        .tooltip = "Corrects unbalanced saturation from per-channel grading.",
        .min = 0.f,
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .parse = [](float value) { return value * 0.01f; },
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeBlowoutCorrection",
        .binding = &shader_injection.color_grade_blowout_restoration,
        .default_value = 50.f,
        .label = "Blowout Restoration",
        .section = "Scene Grading",
        .tooltip = "Restores color from blowout from per-channel grading.",
        .min = 0.f,
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .parse = [](float value) { return value * 0.01f; },
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeExposure",
        .binding = &shader_injection.tone_map_exposure,
        .default_value = 1.f,
        .label = "Exposure",
        .section = "Custom Color Grading",
        .max = 2.f,
        .format = "%.2f",
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeHighlights",
        .binding = &shader_injection.tone_map_highlights,
        .default_value = 50.f,
        .label = "Highlights",
        .section = "Custom Color Grading",
        .max = 100.f,
        .parse = [](float value) { return value * 0.02f; },
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeShadows",
        .binding = &shader_injection.tone_map_shadows,
        .default_value = 50.f,
        .label = "Shadows",
        .section = "Custom Color Grading",
        .max = 100.f,
        .parse = [](float value) { return value * 0.02f; },
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeContrast",
        .binding = &shader_injection.tone_map_contrast,
        .default_value = 50.f,
        .label = "Contrast",
        .section = "Custom Color Grading",
        .max = 100.f,
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeSaturation",
        .binding = &shader_injection.tone_map_saturation,
        .default_value = 50.f,
        .label = "Saturation",
        .section = "Custom Color Grading",
        .max = 100.f,
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeHighlightSaturation",
        .binding = &shader_injection.tone_map_highlight_saturation,
        .default_value = 50.f,
        .label = "Highlight Saturation",
        .section = "Custom Color Grading",
        .tooltip = "Adds or removes highlight color.",
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type >= 1; },
        .parse = [](float value) { return value * 0.02f; },
        .is_visible = []() { return current_settings_mode >= 1; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeBlowout",
        .binding = &shader_injection.tone_map_blowout,
        .default_value = 0.f,
        .label = "Blowout",
        .section = "Custom Color Grading",
        .tooltip = "Controls highlight desaturation due to overexposure.",
        .max = 100.f,
        .parse = [](float value) { return value * 0.01f; },
    },
    new renodx::utils::settings::Setting{
        .key = "ColorGradeFlare",
        .binding = &shader_injection.tone_map_flare,
        .default_value = 0.f,
        .label = "Flare",
        .section = "Custom Color Grading",
        .tooltip = "Flare/Glare Compensation",
        .max = 100.f,
        .is_enabled = []() { return shader_injection.tone_map_type == 3; },
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxLensDistortion",
        .binding = &shader_injection.custom_lens_distortion,
        .default_value = 50.f,
        .label = "Lens Distortion",
        .section = "Effects",
        .max = 100.f,
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxChromaticAberration",
        .binding = &shader_injection.custom_chromatic_aberration,
        .default_value = 50.f,
        .label = "Chromatic Aberration",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxBloom",
        .binding = &shader_injection.custom_bloom,
        .default_value = 50.f,
        .label = "Bloom",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxSunBloom",
        .binding = &shader_injection.custom_sun_bloom,
        .default_value = 50.f,
        .label = "Sun Bloom",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxCorona",
        .binding = &shader_injection.custom_corona,
        .default_value = 50.f,
        .label = "Corona",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxLensFlare",
        .binding = &shader_injection.custom_lens_flare,
        .default_value = 50.f,
        .label = "Lens Flare",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    // new renodx::utils::settings::Setting{
    //     .key = "FxLightStreaks",
    //     .binding = &shader_injection.custom_light_streaks,
    //     .default_value = 50.f,
    //     .label = "Light Streaks",
    //     .section = "Effects",
    //     .parse = [](float value) { return value * 0.02f; },
    // },
    new renodx::utils::settings::Setting{
        .key = "FxVignette",
        .binding = &shader_injection.custom_vignette,
        .default_value = 50.f,
        .label = "Vignette",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxFilmGrain",
        .binding = &shader_injection.custom_film_grain,
        .default_value = 50.f,
        .label = "Film Grain",
        .section = "Effects",
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .key = "FxDithering",
        .binding = &shader_injection.custom_dithering,
        .default_value = 0.f,
        .label = "Dithering",
        .section = "Effects",
        .is_enabled = []() { return false; },
        .parse = [](float value) { return value * 0.02f; },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Reset All",
        .section = "Options",
        .group = "button-line-1",
        .on_change = []() { renodx::utils::settings::ResetSettings(); },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Discord",
        .section = "Options",
        .group = "button-line-2",
        .tint = 0x5865F2,
        .on_change = []() {
          renodx::utils::platform::LaunchURL("https://discord.gg/", "F6AUTeWJHM");
        },
    },
    new renodx::utils::settings::Setting{
        .value_type = renodx::utils::settings::SettingValueType::BUTTON,
        .label = "Github",
        .section = "Options",
        .group = "button-line-2",
        .on_change = []() {
          renodx::utils::platform::LaunchURL("https://github.com/clshortfuse/renodx");
        },
    },
};

void OnPresetOff() {
  renodx::utils::settings::UpdateSettings({
      {"ToneMapType", 0.f},
      {"ToneMapPeakNits", 203.f},
      {"ToneMapGameNits", 203.f},
      {"ToneMapUINits", 203.f},
      {"GammaCorrection", 0.f},
      {"ToneMapHueProcessor", 0.f},
      {"ToneMapWhiteClip", 1.f},
      {"ColorGradeStrength", 100.f},
      {"ColorGradeHueCorrection", 0.f},
      {"ColorGradeSaturationCorrection", 0.f},
      {"ColorGradeBlowoutCorrection", 0.f},

      {"ColorGradeExposure", 1.f},
      {"ColorGradeHighlights", 50.f},
      {"ColorGradeShadows", 50.f},
      {"ColorGradeContrast", 50.f},
      {"ColorGradeSaturation", 50.f},
      {"ColorGradeHighlightSaturation", 50.f},
      {"ColorGradeBlowout", 0.f},
      {"ColorGradeFlare", 0.f},

      {"FxLensDistortion", 50.f},
      {"FxChromaticAberration", 50.f},
      {"FxBloom", 50.f},
      {"FxSunBloom", 50.f},
      {"FxCorona", 50.f},
      {"FxLensFlare", 50.f},
      {"FxVignette", 50.f},
      {"FxFilmGrain", 50.f},
      {"FxDithering", 0.f},

  });
}

bool initialized = false;

}  // namespace

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX for GTA:V Enhanced";

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;

      if (!initialized) {
        renodx::mods::shader::force_pipeline_cloning = true;
        renodx::mods::shader::expected_constant_buffer_space = 50;
        renodx::mods::shader::expected_constant_buffer_index = 13;
        renodx::mods::shader::allow_multiple_push_constants = true;

        renodx::mods::swapchain::SetUseHDR10(true);
        renodx::mods::swapchain::expected_constant_buffer_index = 13;
        renodx::mods::swapchain::expected_constant_buffer_space = 50;
        renodx::mods::swapchain::use_resource_cloning = true;
        renodx::mods::swapchain::swap_chain_proxy_vertex_shader = __swap_chain_proxy_vertex_shader;
        renodx::mods::swapchain::swap_chain_proxy_pixel_shader = __swap_chain_proxy_pixel_shader;
        renodx::mods::swapchain::ignored_window_class_names = {"RGSCD3D12_TEMPWINDOW"};

        renodx::mods::swapchain::force_borderless = true;
        renodx::mods::swapchain::prevent_full_screen = true;

        renodx::utils::random::binds.push_back(&shader_injection.custom_random);

        renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
            .old_format = reshade::api::format::b8g8r8a8_unorm,
            .new_format = reshade::api::format::r16g16b16a16_float,
            .use_resource_view_cloning = true,
            .aspect_ratio = renodx::mods::swapchain::SwapChainUpgradeTarget::BACK_BUFFER,
            .usage_include = reshade::api::resource_usage::render_target,
            .use_resource_view_cloning_and_upgrade = true,
        });

        renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
            .old_format = reshade::api::format::b8g8r8a8_unorm,
            .new_format = reshade::api::format::r16g16b16a16_float,
            .use_resource_view_cloning = true,
            .dimensions = {
                .width = 512,
                .height = 1024,
            },
            .usage_include = reshade::api::resource_usage::render_target,
            .use_resource_view_cloning_and_upgrade = true,
        });

        renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
            .old_format = reshade::api::format::b8g8r8a8_unorm,
            .new_format = reshade::api::format::r16g16b16a16_float,
            .use_resource_view_cloning = true,
            .dimensions = {
                .width = 480,
                .height = 192,
            },
            .usage_include = reshade::api::resource_usage::render_target,
            .use_resource_view_cloning_and_upgrade = true,
        });

        renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
            .old_format = reshade::api::format::b8g8r8a8_unorm,
            .new_format = reshade::api::format::r16g16b16a16_float,
            .use_resource_view_cloning = true,
            .dimensions = {
                .width = 960,
                .height = 384,
            },
            .usage_include = reshade::api::resource_usage::render_target,
            .use_resource_view_cloning_and_upgrade = true,
        });
      }

      initialized = true;

      break;
    case DLL_PROCESS_DETACH:
      reshade::unregister_event<reshade::addon_event::update_descriptor_tables>(OnUpdateDescriptorTables);
      reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
      reshade::unregister_addon(h_module);
      break;
  }

  renodx::utils::random::Use(fdw_reason);
  renodx::utils::settings::Use(fdw_reason, &settings, &OnPresetOff);

  renodx::mods::swapchain::Use(fdw_reason, &shader_injection);
  renodx::mods::shader::Use(fdw_reason, custom_shaders, &shader_injection);

  if (fdw_reason == DLL_PROCESS_ATTACH) {
    rain_descriptor_debug_count = 0;
    rain_descriptor_debug_suppressed_count = 0;
    reshade::register_event<reshade::addon_event::update_descriptor_tables>(OnUpdateDescriptorTables);
    reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
    reshade::log::message(reshade::log::level::info, "GTAV rain descriptor debug attached.");
  }

  return TRUE;
}
