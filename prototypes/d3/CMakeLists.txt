# Prototype D3: binding engine follow-up measurement code (§30)
# this target is standalone: no runtime module links it
#
# D3 deliberately links Prototype D2's storage and graph harness rather than
# reimplementing it, so D3's numbers stay comparable to D2's by construction (§30)

# R1: the typed storage accounting that reconciles AD-04b's arithmetic (§28.4)
add_executable(albor-prototype-d3-storage storage_accounting.cpp)
target_link_libraries(albor-prototype-d3-storage PRIVATE albor-prototype-d2)
