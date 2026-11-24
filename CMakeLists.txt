cmake_minimum_required(VERSION 3.22)
project(ANKS_FX VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(DEFINED ANKS_JUCE_DIR)
    add_subdirectory(${ANKS_JUCE_DIR} JUCE)
else()
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ANKS_ALL_TARGETS "")

# anks_fx(<dossier> <code 4 lettres> <nom affiché> <catégorie VST3> <MIDI TRUE/FALSE>)
function(anks_fx target code product category midi)
    juce_add_plugin(${target}
        COMPANY_NAME "ANKS Labs"
        COMPANY_WEBSITE "https://github.com/olivershadd-star"
        BUNDLE_ID com.ankslabs.${target}
        PLUGIN_MANUFACTURER_CODE Anks
        PLUGIN_CODE ${code}
        FORMATS VST3
        PRODUCT_NAME "${product}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT ${midi}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx ${category})
    target_sources(${target} PRIVATE plugins/${target}/Plugin.cpp)
    target_include_directories(${target} PRIVATE core)
    target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    set(ANKS_ALL_TARGETS ${ANKS_ALL_TARGETS} ${target} PARENT_SCOPE)
endfunction()

# La suite ANKS FX
anks_fx(Ring        Fx01 "ANKS Ring"         Modulation TRUE)
anks_fx(Shaper      Fx02 "ANKS Shaper"       Distortion FALSE)
anks_fx(Shift       Fx03 "ANKS Shift"        Modulation FALSE)
anks_fx(Gate        Fx04 "ANKS Gate"         Dynamics   FALSE)
anks_fx(Formant     Fx05 "ANKS Formant"      Filter     FALSE)
anks_fx(PhaseMod    Fx06 "ANKS PhaseMod"     Modulation FALSE)
anks_fx(Resonator   Fx07 "ANKS Resonator"    Filter     TRUE)
anks_fx(Grain       Fx08 "ANKS Grain"        Delay      FALSE)
anks_fx(Stutter     Fx09 "ANKS Stutter"      Delay      FALSE)
anks_fx(Tape        Fx10 "ANKS Tape"         Modulation FALSE)
anks_fx(Crush       Fx11 "ANKS Crush"        Distortion FALSE)
anks_fx(TripleDrive Tdri "ANKS Triple Drive" Distortion FALSE)
anks_fx(Tracker     Fx13 "ANKS Tracker"      Generator  TRUE)
anks_fx(Filter      Fx14 "ANKS Filter"       Filter     FALSE)

message(STATUS "ANKS FX : ${ANKS_ALL_TARGETS}")

# Tests hors ligne (optionnels, non compilés par GitHub Actions)
option(ANKS_TESTS "Compiler les tests hors ligne" OFF)
if(ANKS_TESTS)
    foreach(t ${ANKS_ALL_TARGETS})
        add_executable(test_${t} tests/FxTest.cpp)
        target_link_libraries(test_${t} PRIVATE ${t})
        target_compile_definitions(test_${t} PRIVATE $<TARGET_PROPERTY:${t},COMPILE_DEFINITIONS>)
        target_include_directories(test_${t} PRIVATE $<TARGET_PROPERTY:${t},INCLUDE_DIRECTORIES>)
    endforeach()
endif()
