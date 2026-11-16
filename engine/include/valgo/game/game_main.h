// =============================================================================
// Project Valgo — entry point plumbing.
//
// A game implements `int ValgoGameMain(int argc, char** argv)` and includes this
// header to get the correct entry point for the platform:
//   * Windows GUI subsystem -> wWinMain (provided by the engine library)
//   * console / Linux        -> main (provided by this macro)
// =============================================================================
#pragma once

#if defined(VALGO_PLATFORM_WINDOWS)
#define VALGO_IMPLEMENT_GAME_MAIN() /* wWinMain lives in the engine library */
#else
#define VALGO_IMPLEMENT_GAME_MAIN()              \
    int main(int argc, char** argv) {            \
        return ::ValgoGameMain(argc, argv);      \
    }
#endif

#ifdef __cplusplus
extern "C" {
#endif
int ValgoGameMain(int argc, char** argv);
#ifdef __cplusplus
}
#endif
