# Included only by the Nix derivation to validate its chosen network backends.
add_executable(nix_network_smoke network-smoke.cpp)
target_link_libraries(nix_network_smoke PRIVATE borealis::http borealis::ws)
install(TARGETS nix_network_smoke DESTINATION "${CMAKE_INSTALL_PREFIX}/../nix-support")

add_executable(nix_funchook_smoke funchook-smoke.cpp)
target_link_libraries(nix_funchook_smoke PRIVATE funchook-static)
install(TARGETS nix_funchook_smoke DESTINATION "${CMAKE_INSTALL_PREFIX}/../nix-support")
