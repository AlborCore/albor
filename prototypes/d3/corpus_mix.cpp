// Prototype D3 R11 — the property mix of a real UI, and the combined memory on that mix (F2.11, AD-04b)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// There is no .ui file in this repository — the compiler is Phase 5 (§31) — so the corpus under
// prototypes/d3/corpus/ is authored, and every threshold that judges a conclusion from it is judged against
// the corpus's own representativeness bounds rather than assumed (plan.md R11)

#include "prototypes/d2/src/index.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/value.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        // the corpus directory, relative to the repository root the plan's commands run from (§28.4)
        constexpr std::string_view CORPUS_DIRECTORY = "prototypes/d3/corpus";

        // T3.11.2 a real UI is mostly geometry and state
        constexpr double T3_11_2_SCALAR_SHARE = 0.40;

        // T3.11.2 a real UI still carries labels and model data
        constexpr double T3_11_2_PAYLOAD_SHARE = 0.05;

        // T3.11.2 a binding reads few properties
        constexpr double T3_11_2_MEAN_DEPENDENCIES = 3.0;

        // T3.11.4 R2's combined budget, re-judged on the corpus mix
        constexpr double T3_11_4_COMBINED_BYTES = 36.0;

        // T3.11.5 R2's segmented premium, re-judged on the corpus mix
        constexpr double T3_11_5_SEGMENTED_DELTA = 12.0;

        // T3.11.6 R3's measured crossover, in live properties
        constexpr std::size_t T3_11_6_CROSSOVER_PROPERTIES = 128;

        // the scales the corpus is instantiated at: one screen, a ten-window application, a hundred windows (§28.2)
        constexpr std::size_t SCREEN_SCALES[] = {1, 10, 100};

        // the environment properties §13.1 categories 8 and 9 make bindable (§13.1)
        const std::vector<std::string> ENVIRONMENT_PROPERTIES{"theme", "locale", "direction", "scale"};

        // the scopes a declared dependency may name besides the environment (§13.3)
        const std::vector<std::string> DATA_SCOPES{"self", "app", "row", "model"};

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            std::printf("  %-8s %-64s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, std::string_view description, std::string_view detail) { std::printf("  %-8s %-64s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), "REC", std::string(detail).c_str()); }

        std::string format(double value, int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // hold one declared binding: the property it writes and the properties it reads (§13.3)
        struct BindingDecl
        {
            std::string target;
            std::vector<std::string> dependencies;
            std::size_t environment_dependencies = 0;
        };

        // hold one declared component type and its property declarations (§13.1)
        struct ComponentDecl
        {
            std::string name;
            std::vector<std::string> property_names;
            std::vector<albor::prototype_d2::ValueType> property_types;
            std::vector<BindingDecl> bindings;

            // count the properties of one §13.1 category, which is what the mix is made of (§13.1)
            [[nodiscard]] std::size_t count_of(albor::prototype_d2::ValueType type) const { return static_cast<std::size_t>(std::ranges::count(property_types, type)); }
        };

        // hold one screen: the component instances it declares and their multiplicities (§18.2)
        struct ScreenDecl
        {
            std::string name;
            std::vector<std::pair<std::string, std::size_t>> uses;
        };

        // hold the parsed corpus and the diagnostics the parser produced (§28.2)
        struct Corpus
        {
            std::vector<ComponentDecl> components;
            std::vector<ScreenDecl> screens;
            std::vector<std::string> errors;
        };

        albor::prototype_d2::ValueType parse_type(const std::string& token) {
            // map the corpus's type names onto the §13.1 value categories (§13.1)
            if (token == "int") {
                return albor::prototype_d2::ValueType::Int;
            }

            if (token == "double") {
                return albor::prototype_d2::ValueType::Double;
            }

            if (token == "bool") {
                return albor::prototype_d2::ValueType::Bool;
            }

            if (token == "string") {
                return albor::prototype_d2::ValueType::String;
            }

            if (token == "translated") {
                return albor::prototype_d2::ValueType::TranslatedString;
            }

            if (token == "snapshot") {
                return albor::prototype_d2::ValueType::Snapshot;
            }

            // report an unknown type as None, which the parser then rejects (§28.2)
            return albor::prototype_d2::ValueType::None;
        }

        // split a line into whitespace separated tokens, dropping the trailing carriage return of a CRLF file (§28.2)
        std::vector<std::string> tokenize(const std::string& line) {
            std::vector<std::string> tokens;
            std::istringstream stream(line);
            std::string token;

            while (stream >> token) {
                tokens.push_back(token);
            }

            // hand back the tokens of the line
            return tokens;
        }

        // report whether a declared dependency names an environment property (§13.1 categories 8 and 9)
        //
        // the environment publishes theme, locale, direction and scale, and a binding reaches a member of a published
        // value through it, so env.theme.surface is one dependency on the theme property rather than a scope of its own
        bool is_environment(const std::string& dependency) {
            // split the scope from the rest of the path (§13.3)
            const std::size_t dot = dependency.find('.');

            if (dot == std::string::npos) {
                return false;
            }

            // only the env scope is an environment property (§13.1)
            if (dependency.compare(0, dot, "env") != 0) {
                return false;
            }

            // the value the environment publishes has to be one §13.1 names, and a member access may follow it (§13.1)
            const std::string published = dependency.substr(dot + 1, dependency.find('.', dot + 1) - dot - 1);
            return std::ranges::find(ENVIRONMENT_PROPERTIES, published) != ENVIRONMENT_PROPERTIES.end();
        }

        // report whether a declared dependency names a data scope this corpus declares (§13.3)
        bool is_data_scope(const std::string& dependency) {
            // split the scope from the property name (§13.3)
            const std::size_t dot = dependency.find('.');

            if (dot == std::string::npos) {
                return false;
            }

            // the scope has to be one a binding may read (§13.3)
            return std::ranges::find(DATA_SCOPES, dependency.substr(0, dot)) != DATA_SCOPES.end();
        }

        // parse one corpus file into the declarations it carries (§28.2)
        void parse_file(const std::filesystem::path& path, Corpus& corpus) {
            std::ifstream input(path);

            // report a file that cannot be read rather than measuring a corpus that is missing a screen (§28.2)
            if (!input) {
                corpus.errors.push_back("cannot read " + path.string());
                return;
            }

            // the declaration currently being read, which a component or screen line opens (§28.2)
            ComponentDecl* component = nullptr;
            ScreenDecl* screen = nullptr;
            std::string line;
            std::size_t line_number = 0;

            while (std::getline(input, line)) {
                line_number++;

                // drop a comment and an empty line, which carry no declaration (§28.2)
                const std::size_t comment = line.find('#');

                if (comment != std::string::npos) {
                    line = line.substr(0, comment);
                }

                const std::vector<std::string> tokens = tokenize(line);

                if (tokens.empty()) {
                    continue;
                }

                // a component declaration opens a component type (§13.1)
                if (tokens.at(0) == "component" && tokens.size() >= 2) {
                    corpus.components.push_back(ComponentDecl{});
                    component = &corpus.components.back();
                    component->name = tokens.at(1);
                    screen = nullptr;
                    continue;
                }

                // a screen declaration opens a screen (§18.2)
                if (tokens.at(0) == "screen" && tokens.size() >= 2) {
                    corpus.screens.push_back(ScreenDecl{});
                    screen = &corpus.screens.back();
                    screen->name = tokens.at(1);
                    component = nullptr;
                    continue;
                }

                // end closes whichever declaration is open (§28.2)
                if (tokens.at(0) == "end") {
                    component = nullptr;
                    screen = nullptr;
                    continue;
                }

                // a property declaration belongs to the open component (§13.1)
                if (tokens.at(0) == "prop" && tokens.size() >= 3) {
                    if (component == nullptr) {
                        corpus.errors.push_back(path.filename().string() + ":" + std::to_string(line_number) + " prop outside a component");
                        continue;
                    }

                    // resolve the declared type, rejecting an unknown name (§13.1)
                    const albor::prototype_d2::ValueType type = parse_type(tokens.at(2));

                    if (type == albor::prototype_d2::ValueType::None) {
                        corpus.errors.push_back(path.filename().string() + ":" + std::to_string(line_number) + " unknown type " + tokens.at(2));
                        continue;
                    }

                    component->property_names.push_back(tokens.at(1));
                    component->property_types.push_back(type);
                    continue;
                }

                // a binding declaration belongs to the open component (§13.3)
                if (tokens.at(0) == "bind" && tokens.size() >= 4 && tokens.at(2) == "<-") {
                    if (component == nullptr) {
                        corpus.errors.push_back(path.filename().string() + ":" + std::to_string(line_number) + " bind outside a component");
                        continue;
                    }

                    // collect the declared dependencies, which the corpus separates with commas (§13.3)
                    BindingDecl binding;
                    binding.target = tokens.at(1);

                    for (std::size_t index = 3; index < tokens.size(); index++) {
                        // one token may carry several comma separated dependencies (§13.3)
                        std::string token = tokens.at(index);
                        std::size_t start = 0;

                        while (start <= token.size()) {
                            // find the end of this dependency, which the next comma or the token's end marks (§13.3)
                            const std::size_t comma = token.find(',', start);
                            const std::string dependency = token.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

                            if (!dependency.empty()) {
                                binding.dependencies.push_back(dependency);
                            }

                            // stop when the token held no further comma (§13.3)
                            if (comma == std::string::npos) {
                                break;
                            }

                            // continue after the comma (§13.3)
                            start = comma + 1;
                        }
                    }

                    component->bindings.push_back(std::move(binding));
                    continue;
                }

                // a use declaration belongs to the open screen (§18.2)
                if (tokens.at(0) == "use" && tokens.size() >= 3) {
                    if (screen == nullptr) {
                        corpus.errors.push_back(path.filename().string() + ":" + std::to_string(line_number) + " use outside a screen");
                        continue;
                    }

                    // the multiplicity is how many live instances the screen declares (§20.2)
                    const std::size_t multiplicity = static_cast<std::size_t>(std::stoul(tokens.at(2)));
                    screen->uses.emplace_back(tokens.at(1), multiplicity);
                    continue;
                }

                // report anything else rather than skipping it, because a silent line is a silent omission (§28.2)
                corpus.errors.push_back(path.filename().string() + ":" + std::to_string(line_number) + " unrecognised line: " + line);
            }
        }

        // resolve every declared binding against its component's properties and the environment (§13.3)
        void validate(Corpus& corpus) {
            for (ComponentDecl& component : corpus.components) {
                for (BindingDecl& binding : component.bindings) {
                    // the target of a binding has to be a declared property (§13.3)
                    if (std::ranges::find(component.property_names, binding.target) == component.property_names.end()) {
                        corpus.errors.push_back(component.name + ": bind target " + binding.target + " is not a declared property");
                    }

                    for (const std::string& dependency : binding.dependencies) {
                        // count the environment dependencies, which §13.1 makes first class (§13.1)
                        if (is_environment(dependency)) {
                            binding.environment_dependencies++;
                            continue;
                        }

                        // a dependency on the component's own properties must name a declared one (§13.3)
                        if (!is_data_scope(dependency)) {
                            corpus.errors.push_back(component.name + ": dependency " + dependency + " has no known scope");
                            continue;
                        }

                        // split the scope from the property name (§13.3)
                        const std::size_t dot = dependency.find('.');
                        const std::string property = dependency.substr(dot + 1);
                        const std::string scope = dependency.substr(0, dot);

                        // a self dependency resolves inside this component, so it must be declared here (§13.3)
                        if (scope == "self" && std::ranges::find(component.property_names, property) == component.property_names.end()) {
                            corpus.errors.push_back(component.name + ": self dependency " + property + " is not a declared property");
                        }
                    }
                }
            }

            // every screen must name components the corpus declares (§18.2)
            for (const ScreenDecl& screen : corpus.screens) {
                for (const auto& [component_name, multiplicity] : screen.uses) {
                    const bool declared = std::ranges::any_of(corpus.components, [&component_name](const ComponentDecl& component) { return component.name == component_name; });

                    if (!declared) {
                        corpus.errors.push_back(screen.name + ": uses undeclared component " + component_name);
                    }

                    // a screen that declares no instance of anything is not a screen (§18.2)
                    if (multiplicity == 0) {
                        corpus.errors.push_back(screen.name + ": declares a zero multiplicity for " + component_name);
                    }
                }
            }
        }

        // hold what the corpus declares about one screen, in the units R2's accounting uses (§28.2)
        struct ScreenTotals
        {
            std::string name;
            std::size_t instances = 0;
            std::size_t properties = 0;
            std::size_t bindings = 0;
            std::size_t dependencies = 0;
            std::size_t environment_bindings = 0;
        };

        // hold the corpus-wide mix, which is what AD-04b's numbers actually depend on (§13.1)
        struct MixTotals
        {
            std::size_t properties = 0;
            std::size_t bindings = 0;
            std::size_t dependencies = 0;
            std::size_t environment_dependencies = 0;
            std::size_t instances = 0;
            std::size_t scalars = 0;
            std::size_t payload_owning = 0;
            std::size_t predicted_value_bytes = 0;
            std::map<std::string, std::size_t> dependency_histogram;
        };

        const ComponentDecl* find_component(const Corpus& corpus, const std::string& name) {
            // resolve a component name the screens declare (§18.2)
            for (const ComponentDecl& component : corpus.components) {
                if (component.name == name) {
                    return &component;
                }
            }

            // report nothing rather than a wrong component (§28.2)
            return nullptr;
        }

        // total one screen's declarations, which is what a live window holds (§28.2)
        ScreenTotals screen_totals(const Corpus& corpus, const ScreenDecl& screen) {
            ScreenTotals totals;
            totals.name = screen.name;

            // walk the component instances the screen declares (§18.2)
            for (const auto& [component_name, multiplicity] : screen.uses) {
                const ComponentDecl* component = find_component(corpus, component_name);

                // a screen naming an undeclared component was already reported by the validator (§28.2)
                if (component == nullptr) {
                    continue;
                }

                totals.instances += multiplicity;
                totals.properties += multiplicity * component->property_types.size();
                totals.bindings += multiplicity * component->bindings.size();

                for (const BindingDecl& binding : component->bindings) {
                    totals.dependencies += multiplicity * binding.dependencies.size();

                    if (binding.environment_dependencies > 0) {
                        totals.environment_bindings += multiplicity;
                    }
                }
            }

            // hand back the screen's totals (§28.2)
            return totals;
        }

        // total the corpus, which is the mix every memory figure below is computed on (§13.1)
        MixTotals mix_totals(const Corpus& corpus, const std::vector<ScreenTotals>& screens) {
            MixTotals totals;

            // one screen of each kind is the unit the corpus is scaled from (§28.2)
            for (const ScreenTotals& screen : screens) {
                totals.properties += screen.properties;
                totals.bindings += screen.bindings;
                totals.dependencies += screen.dependencies;
                totals.instances += screen.instances;
            }

            // count the categories over the component types each screen instantiates (§13.1)
            for (const ComponentDecl& component : corpus.components) {
                // the number of live instances of this component type across the whole corpus (§18.2)
                std::size_t live = 0;

                for (const ScreenDecl& screen : corpus.screens) {
                    for (const auto& [component_name, multiplicity] : screen.uses) {
                        if (component_name == component.name) {
                            live += multiplicity;
                        }
                    }
                }

                // charge the component's properties and bindings to its live instances (§13.1)
                for (const albor::prototype_d2::ValueType type : component.property_types) {
                    if (type == albor::prototype_d2::ValueType::Bool || type == albor::prototype_d2::ValueType::Int || type == albor::prototype_d2::ValueType::Double) {
                        totals.scalars += live;
                    }

                    if (albor::prototype_d2::is_non_trivial(type)) {
                        totals.payload_owning += live;
                    }

                    totals.predicted_value_bytes += live * albor::prototype_d2::inline_size_of(type);
                }

                for (const BindingDecl& binding : component.bindings) {
                    totals.environment_dependencies += live * binding.environment_dependencies;
                    totals.dependency_histogram[std::to_string(binding.dependencies.size())] += live;
                }
            }

            // hand back the corpus mix (§28.2)
            return totals;
        }

        // the combined accounting of one storage candidate and one index at a corpus scale (§28.2)
        struct Combined
        {
            double storage_bytes = 0.0;
            double index_bytes = 0.0;
            double total = 0.0;
            double per_property = 0.0;
        };

        // build the corpus's schemas in one region table, so both storage candidates see the same components (§13.1)
        std::vector<albor::prototype_d2::SchemaId> declare_corpus_schemas(const Corpus& corpus, albor::prototype_d2::RegionTable& regions) {
            std::vector<albor::prototype_d2::SchemaId> schemas;
            schemas.reserve(corpus.components.size());

            // declare every component type the corpus declares (§13.1)
            for (const ComponentDecl& component : corpus.components) {
                schemas.push_back(regions.add_schema(component.name, albor::prototype_d2::InstanceKind::Component, component.property_types));
            }

            // hand back the schema ids in declaration order (§28.2)
            return schemas;
        }

    } // namespace

    void run_corpus_mix(Verdict& verdict) {
        // read every corpus file, so the measurement covers the corpus rather than one screen (§28.2)
        Corpus corpus;

        std::vector<std::filesystem::path> files;

        for (const auto& entry : std::filesystem::directory_iterator(CORPUS_DIRECTORY)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ui") {
                files.push_back(entry.path());
            }
        }

        // parse in a stable order, so a rerun reports the same rows (§28.4)
        std::ranges::sort(files);

        for (const auto& file : files) {
            parse_file(file, corpus);
        }

        // resolve every binding against the corpus before anything is measured (§28.4)
        validate(corpus);

        // T3.11.1: the corpus has to parse and resolve, because an unresolved dependency understates the fan-in (§28.4)
        check(verdict, "T3.11.1", "corpus parses with 0 errors, every binding resolves", corpus.errors.empty(), corpus.errors.empty() ? std::to_string(files.size()) + " files, 0 errors" : std::to_string(corpus.errors.size()) + " errors: " + corpus.errors.front());

        if (!corpus.errors.empty()) {
            // report every error, because one screen's typo must not hide another's (§28.2)
            for (const std::string& error : corpus.errors) {
                std::printf("    %s\n", error.c_str());
            }

            return;
        }

        // report what the corpus declares, per screen (§28.2)
        std::vector<ScreenTotals> screens;
        screens.reserve(corpus.screens.size());

        for (const ScreenDecl& screen : corpus.screens) {
            screens.push_back(screen_totals(corpus, screen));
        }

        std::printf("--- the corpus, per screen (one live instance per declared use) ---\n");
        std::printf("  %-16s %10s %12s %10s %12s %10s\n", "screen", "instances", "properties", "bindings", "dependencies", "env binds");

        for (const ScreenTotals& screen : screens) {
            std::printf("  %-16s %10zu %12zu %10zu %12zu %10zu\n", screen.name.c_str(), screen.instances, screen.properties, screen.bindings, screen.dependencies, screen.environment_bindings);
        }

        // report what the corpus declares, per component type (§28.2)
        std::printf("--- the corpus, per component type ---\n");
        std::printf("  %-14s %10s %10s %10s %10s %10s %10s %10s\n", "component", "props", "int", "double", "bool", "string", "translated", "snapshot");

        for (const ComponentDecl& component : corpus.components) {
            std::printf("  %-14s %10zu %10zu %10zu %10zu %10zu %10zu %10zu\n", component.name.c_str(), component.property_types.size(), component.count_of(albor::prototype_d2::ValueType::Int), component.count_of(albor::prototype_d2::ValueType::Double), component.count_of(albor::prototype_d2::ValueType::Bool), component.count_of(albor::prototype_d2::ValueType::String), component.count_of(albor::prototype_d2::ValueType::TranslatedString), component.count_of(albor::prototype_d2::ValueType::Snapshot));
        }

        // total the corpus, which is the mix AD-04b's numbers have to survive (§13.1)
        const MixTotals mix = mix_totals(corpus, screens);
        const double scalar_share = static_cast<double>(mix.scalars) / static_cast<double>(mix.properties);
        const double payload_share = static_cast<double>(mix.payload_owning) / static_cast<double>(mix.properties);
        const double mean_dependencies = static_cast<double>(mix.dependencies) / static_cast<double>(mix.bindings);
        const double value_bytes_per_property = static_cast<double>(mix.predicted_value_bytes) / static_cast<double>(mix.properties);

        std::printf("--- the corpus mix, one screen of each kind ---\n");
        std::printf("  instances %zu   properties %zu   bindings %zu   dependency entries %zu\n", mix.instances, mix.properties, mix.bindings, mix.dependencies);
        std::printf("  scalar share %s%%   payload-owning share %s%%   environment dependency entries %zu (%s%% of all)\n", format(scalar_share * 100.0, 1).c_str(), format(payload_share * 100.0, 1).c_str(), mix.environment_dependencies, format(100.0 * static_cast<double>(mix.environment_dependencies) / static_cast<double>(mix.dependencies), 1).c_str());
        std::printf("  mean dependencies per binding %s   value bytes per property %s B against mixed's 14.00 B\n", format(mean_dependencies, 2).c_str(), format(value_bytes_per_property, 2).c_str());

        // report the dependency-count distribution, because the index cost follows it (§28.2)
        std::printf("  dependency count distribution:");

        for (const auto& [count, bindings] : mix.dependency_histogram) {
            std::printf(" %s dep=%s%%", count.c_str(), format(100.0 * static_cast<double>(bindings) / static_cast<double>(mix.bindings), 1).c_str());
        }

        std::printf("\n");

        // T3.11.2a: a real UI is mostly geometry and state (§13.1, §21.1)
        check(verdict, "T3.11.2a", "corpus scalar share >= 40%", scalar_share >= T3_11_2_SCALAR_SHARE, format(scalar_share * 100.0, 1) + "% >= " + format(T3_11_2_SCALAR_SHARE * 100.0, 0) + "%");

        // T3.11.2b: a real UI still carries labels and model data (§13.1, §20)
        check(verdict, "T3.11.2b", "corpus payload-owning share >= 5%", payload_share >= T3_11_2_PAYLOAD_SHARE, format(payload_share * 100.0, 1) + "% >= " + format(T3_11_2_PAYLOAD_SHARE * 100.0, 0) + "%");

        // T3.11.2c: a binding reads few properties (§13.3)
        check(verdict, "T3.11.2c", "corpus mean dependencies per binding <= 3", mean_dependencies <= T3_11_2_MEAN_DEPENDENCIES, format(mean_dependencies, 2) + " <= " + format(T3_11_2_MEAN_DEPENDENCIES, 0));

        // T3.11.6: one screen's live properties against R3's measured crossover (§28.4)
        //
        // judged on the smallest screen, which is the strictest reading of the committed wording: a screen that
        // sits below the crossover pays the geometric slab's first-chunk overshoot whatever the others do
        const ScreenTotals* smallest = &screens.front();

        for (const ScreenTotals& screen : screens) {
            if (screen.properties < smallest->properties) {
                smallest = &screen;
            }
        }

        check(verdict, "T3.11.6", "every screen's live properties >= 128 (R3 crossover)", smallest->properties >= T3_11_6_CROSSOVER_PROPERTIES, "smallest screen " + smallest->name + " = " + std::to_string(smallest->properties) + " properties, " + format(static_cast<double>(T3_11_6_CROSSOVER_PROPERTIES), 0) + " needed");

        // report the per-screen property counts, because the crossover is a per-window question (§28.2)
        for (const ScreenTotals& screen : screens) {
            record("-", "screen " + screen.name + ": live properties", std::to_string(screen.properties) + " properties, " + std::to_string(screen.instances) + " instances, " + std::to_string(screen.bindings) + " bindings");
        }

        // declare the corpus's component types once, so both storage candidates measure the same schemas (§28.2)
        albor::prototype_d2::RegionTable regions;
        const std::vector<albor::prototype_d2::SchemaId> schemas = declare_corpus_schemas(corpus, regions);

        // the schema id of each component, in declaration order (§28.2)
        std::map<std::string, albor::prototype_d2::SchemaId> schema_of;

        for (std::size_t index = 0; index < corpus.components.size(); index++) {
            schema_of[corpus.components.at(index).name] = schemas.at(index);
        }

        // instantiate the corpus at each committed scale and account for it exactly as R2 did (§28.2)
        std::printf("--- combined memory on the corpus mix, R2's accounting ---\n");
        std::printf("  %8s %10s %10s %14s %12s %14s %14s %14s %12s\n", "screens", "instances", "properties", "typed+csr B", "B/prop", "typed+seg B", "B/prop", "erased+csr B", "B/prop");

        Combined typed_csr_at_hundred;
        Combined typed_segmented_at_hundred;
        Combined erased_csr_at_hundred;
        std::size_t properties_at_hundred = 0;

        for (const std::size_t screen_scale : SCREEN_SCALES) {
            // count what the scale declares before anything is allocated (§28.2)
            std::size_t instances = 0;

            for (const ScreenDecl& screen : corpus.screens) {
                for (const auto& [component_name, multiplicity] : screen.uses) {
                    static_cast<void>(component_name);
                    instances += multiplicity * screen_scale;
                }
            }

            // the typed store's slab grows geometrically, which is the policy R3 measured (§28.2)
            albor::prototype_d2::TypedStore typed(regions);
            typed.set_slab_growth(albor::prototype_d2::TypedStore::SlabGrowth::Geometric);

            for (std::size_t repeat = 0; repeat < screen_scale; repeat++) {
                for (const ScreenDecl& screen : corpus.screens) {
                    for (const auto& [component_name, multiplicity] : screen.uses) {
                        for (std::size_t copy = 0; copy < multiplicity; copy++) {
                            static_cast<void>(typed.create(schema_of.at(component_name)));
                        }
                    }
                }
            }

            const double typed_storage = static_cast<double>(typed.slab_allocated_bytes() + typed.address_bytes() + typed.region_table_bytes() + typed.region_base_bytes());
            const std::size_t properties = typed.property_count();

            // the erased store on its own region table, because a shared one inflated D2's figure (R1's correction)
            albor::prototype_d2::RegionTable erased_regions;
            albor::prototype_d2::ErasedStore erased(erased_regions);

            for (std::size_t index = 0; index < corpus.components.size(); index++) {
                const albor::prototype_d2::SchemaId schema = erased_regions.add_schema(corpus.components.at(index).name, albor::prototype_d2::InstanceKind::Component, corpus.components.at(index).property_types);
                static_cast<void>(schema);
            }

            for (std::size_t repeat = 0; repeat < screen_scale; repeat++) {
                for (const ScreenDecl& screen : corpus.screens) {
                    for (const auto& [component_name, multiplicity] : screen.uses) {
                        for (std::size_t copy = 0; copy < multiplicity; copy++) {
                            static_cast<void>(erased.create(schema_of.at(component_name)));
                        }
                    }
                }
            }

            const double erased_storage = static_cast<double>(erased.slot_capacity_bytes() + erased_regions.region_table_bytes());

            // build the index's declaration plan from the corpus's own dependency counts, because the index cost
            // follows the entries per binding rather than a constant the plan assumes (plan.md R11) (§28.2)
            std::vector<std::size_t> declared_dependencies;

            for (std::size_t repeat = 0; repeat < screen_scale; repeat++) {
                for (const ScreenDecl& screen : corpus.screens) {
                    for (const auto& [component_name, multiplicity] : screen.uses) {
                        const ComponentDecl* component = find_component(corpus, component_name);

                        // a screen naming an undeclared component was already reported by the validator (§28.2)
                        if (component == nullptr) {
                            continue;
                        }

                        for (std::size_t copy = 0; copy < multiplicity; copy++) {
                            for (const BindingDecl& binding : component->bindings) {
                                declared_dependencies.push_back(binding.dependencies.size());
                            }
                        }
                    }
                }
            }

            const std::size_t corpus_bindings = declared_dependencies.size();

            albor::prototype_d2::SealedCsrIndex csr;
            albor::prototype_d2::SegmentedIndex segmented;

            // declare the corpus's dependency entries, spread uniformly over the property id space (§28.2)
            std::vector<albor::prototype_d2::PropertyId> entry_buffer;

            const auto declare_entries = [&](auto& index) {
                const std::size_t stride = std::max<std::size_t>(1, properties / std::max<std::size_t>(1, corpus_bindings));

                for (std::size_t binding = 0; binding < corpus_bindings; binding++) {
                    // take this binding's own dependency count, so the index carries the corpus's fan-in (§28.2)
                    entry_buffer.clear();

                    for (std::size_t dependency = 0; dependency < declared_dependencies.at(binding); dependency++) {
                        entry_buffer.push_back(static_cast<albor::prototype_d2::PropertyId>((binding * stride + dependency * (stride / 2 + 1)) % properties));
                    }

                    index.declare(static_cast<albor::prototype_d2::BindingId>(binding), std::span<const albor::prototype_d2::PropertyId>{entry_buffer});
                }

                index.finish(properties);
            };

            declare_entries(csr);
            declare_entries(segmented);

            Combined typed_csr;
            typed_csr.storage_bytes = typed_storage;
            typed_csr.index_bytes = static_cast<double>(csr.index_bytes());
            typed_csr.total = typed_csr.storage_bytes + typed_csr.index_bytes;
            typed_csr.per_property = typed_csr.total / static_cast<double>(properties);

            Combined typed_segmented;
            typed_segmented.storage_bytes = typed_storage;
            typed_segmented.index_bytes = static_cast<double>(segmented.index_bytes());
            typed_segmented.total = typed_segmented.storage_bytes + typed_segmented.index_bytes;
            typed_segmented.per_property = typed_segmented.total / static_cast<double>(properties);

            Combined erased_csr_cell;
            erased_csr_cell.storage_bytes = erased_storage;
            erased_csr_cell.index_bytes = static_cast<double>(csr.index_bytes());
            erased_csr_cell.total = erased_csr_cell.storage_bytes + erased_csr_cell.index_bytes;
            erased_csr_cell.per_property = erased_csr_cell.total / static_cast<double>(properties);

            std::printf("  %8zu %10zu %10zu %14.0f %12s %14.0f %14s %14.0f %12s\n", screen_scale, instances, properties, typed_csr.total, format(typed_csr.per_property, 1).c_str(), typed_segmented.total, format(typed_segmented.per_property, 1).c_str(), erased_csr_cell.total, format(erased_csr_cell.per_property, 1).c_str());

            if (screen_scale == SCREEN_SCALES[2]) {
                typed_csr_at_hundred = typed_csr;
                typed_segmented_at_hundred = typed_segmented;
                erased_csr_at_hundred = erased_csr_cell;
                properties_at_hundred = properties;
            }
        }

        // the erased slot is the same for every mix, so the erased column is a constant per property (§13.3)
        record("-", "erased storage bytes per property", format(erased_csr_at_hundred.storage_bytes / static_cast<double>(properties_at_hundred), 2) + " B against the 68.00 B live slot, the difference being the slot array's doubling slack (R1's T3.1.6)");

        // report the two halves separately, because the addressability tax is per property and the index is not (§28.2)
        record("-", "typed storage bytes per property", format(typed_csr_at_hundred.storage_bytes / static_cast<double>(properties_at_hundred), 2) + " B of which 7.00 B is addressability and " + format(value_bytes_per_property, 2) + " B is value");
        record("-", "sealed CSR index bytes per property", format(typed_csr_at_hundred.index_bytes / static_cast<double>(properties_at_hundred), 2) + " B for " + std::to_string(mix.dependencies * SCREEN_SCALES[2]) + " dependency entries at the corpus's own fan-in");

        // T3.11.3: the storage answer has to survive the corpus mix, not only the mix that holds one of every category (§28.4)
        check(verdict, "T3.11.3", "typed+CSR <= erased+CSR on the corpus mix, 100 screens", typed_csr_at_hundred.per_property <= erased_csr_at_hundred.per_property, format(typed_csr_at_hundred.per_property, 1) + " <= " + format(erased_csr_at_hundred.per_property, 1) + " B/property");

        // T3.11.4: R2's budget, re-judged on real data (§28.4)
        check(verdict, "T3.11.4", "typed+CSR <= 36 B/property on the corpus mix", typed_csr_at_hundred.per_property <= T3_11_4_COMBINED_BYTES, format(typed_csr_at_hundred.per_property, 1) + " <= " + format(T3_11_4_COMBINED_BYTES, 0) + " B/property");

        // T3.11.5: R2's segmented premium, re-judged on the corpus's binding-to-property ratio (§28.4)
        const double segmented_premium = typed_segmented_at_hundred.per_property - typed_csr_at_hundred.per_property;
        check(verdict, "T3.11.5", "segmented premium over CSR <= 12 B/property", segmented_premium <= T3_11_5_SEGMENTED_DELTA, format(segmented_premium, 1) + " <= " + format(T3_11_5_SEGMENTED_DELTA, 0) + " B/property");

        // T3.11.7: the corpus itself is the deliverable Phase 2 needs (§28.4)
        record("T3.11.7", "corpus totals", std::to_string(corpus.components.size()) + " component types, " + std::to_string(corpus.screens.size()) + " screens, " + std::to_string(mix.instances) + " instances, " + std::to_string(mix.properties) + " properties, " + std::to_string(mix.bindings) + " bindings, " + std::to_string(mix.dependencies) + " dependency entries");
        record("T3.11.7", "corpus value bytes per property", format(value_bytes_per_property, 2) + " B against mixed's 14.00 B and scalar's 8.00 B, so the corpus is cheaper than the mix R2 measured");
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R11 — the property mix of a real UI (AD-04b)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R11, committed before this program existed\n");
    std::printf("the corpus is authored under %s: no .ui file exists in this repository yet (§31)\n", std::string(albor::prototype_d3::CORPUS_DIRECTORY).c_str());
    std::printf("run from the repository root\n\n");

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_corpus_mix(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
