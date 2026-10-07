#pragma once

#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "runtime/binding_engine.h"
#include "runtime/property_id.h"

namespace albor
{
    // an immutable value a worker thread hands to the UI thread (§10.5, §13.2)
    template <typename T>
    using Snapshot = std::shared_ptr<const T>;

    // the result of a declarative binding evaluation (§7.1, AD-04)
    template <typename T>
    using BindingFn = std::function<T()>;

    // the presence record an animation writes through, which never clobbers a user override (AD-04d)
    struct AnimationPresence
    {
        // whether an animation currently owns the presented value of the property
        bool active = false;

        // how many animation frames have been written through the channel
        std::uint64_t frames = 0;

        // compare two presence records field by field
        friend bool operator==(const AnimationPresence& lhs, const AnimationPresence& rhs) = default;
    };

    // one reactive property of a component, which reads eagerly and writes through three channels (§7.1, AD-24)
    template <typename T>
    class Property
    {
    public:
        // build a property with no identifier and no engine, which a component assigns before it binds (§7.1)
        Property() = default;

        // build a property over an initial value, which the identifier and engine attach later (§7.1)
        explicit Property(T initial_value) :
            m_value(std::move(initial_value)) {}

        // build a fully wired property, which is what a component member declares (§7.1, §7.2)
        Property(T initial_value, PropertyId id, BindingEngine* engine) :
            m_value(std::move(initial_value)), m_id(id), m_engine(engine) {}

        Property(const Property&) = delete;
        Property& operator=(const Property&) = delete;
        Property(Property&&) = delete;
        Property& operator=(Property&&) = delete;
        ~Property() = default;

        // read the current value on the UI thread (§7.1)
        [[nodiscard]] const T& get() const;

        // convert to the current value, which is what a binding body reads (§7.1)
        operator const T&() const { return get(); }

        // assign imperatively, which breaks an active declarative binding (AD-24, §7.3)
        void set(T new_value);

        // assign imperatively through the operator, which is what a component body uses (AD-24)
        Property& operator=(T new_value) {
            // the assignment delegates so the write precedence rule lives in one place (§7.3)
            set(std::move(new_value));
            // return the property itself to allow chaining assignments
            return *this;
        }

        // restore the declarative binding after an imperative override (AD-24, §7.3)
        void rebind();

        // attach a declarative binding that produces the value from its dependencies (§7.3, AD-04c)
        void bind(BindingFn<T> evaluator, std::vector<PropertyId> dependencies);

        // the signature a plain binding function has, which carries its context instead of capturing one (§13.3)
        using StaticEvaluator = T (*)(void* context);

        // attach a declarative binding whose evaluator is a plain function, which generated code installs without allocating (§13.3)
        void bind_static(StaticEvaluator evaluator, void* context, std::span<const PropertyId> dependencies);

        // remove the declarative binding, which leaves the property a plain value (§7.3)
        void unbind();

        // hand out the identifier the dependency graph keys on (§7.2)
        [[nodiscard]] PropertyId id() const;

        // attach the identifier the property was declared with (§7.2)
        void set_id(PropertyId id);

        // attach the engine that settles the property, which a component does once (§7.2)
        void set_engine(BindingEngine* engine);

        // report whether a declarative binding is currently installed and unbroken (AD-24)
        [[nodiscard]] bool bound() const;

        // report whether an imperative write has overridden the binding (AD-24)
        [[nodiscard]] bool overridden() const;

        // write an animated value through the animation presence channel, which never clobbers the user value (AD-04d)
        void animate(T value);

        // hand out the animation presence record, which a test asserts against (AD-04d)
        [[nodiscard]] const AnimationPresence& presence() const;

        // end the animation, which hands the value back to the binding or the override (AD-04d)
        void end_animation();

    private:
        // hand out the value the three channels resolve to (§7.3, AD-24, AD-04d)
        [[nodiscard]] const T& resolved() const;

        // write a value produced by a declarative binding, which is private because it bypasses the precedence rules (§7.3, AD-24)
        void write_bound(T value);

        // run the plain binding function over its context and write the bound channel, which is what the engine receives (§13.3)
        static void evaluate_static(void* context);

        // the value the property presents when no binding and no override apply (§7.1)
        T m_value{};

        // the value the declarative binding last produced, which survives an animation (§7.3, AD-04d)
        T m_bound_value{};

        // the value an imperative write installed, which the animation yields to (AD-24, §7.3)
        T m_override_value{};

        // the value the animation channel writes, which only applies while the animation runs (AD-04d)
        T m_animation_value{};

        // the identifier the dependency graph keys on, which is invalid until a component assigns one (§7.2)
        PropertyId m_id = invalid_property_id();

        // the engine that settles the property, which is null for a detached property (§7.2)
        BindingEngine* m_engine = nullptr;

        // the binding handle the declarative binding registered (§7.3)
        BindingId m_binding = 0;

        // the plain function the declarative binding runs, which is null when the binding captures a callable instead (§13.3)
        StaticEvaluator m_static_evaluator = nullptr;

        // the context the plain function reads, which it cannot capture on its own (§13.3)
        void* m_static_context = nullptr;

        // the animation presence record, which reports whether a channel is running (AD-04d)
        AnimationPresence m_presence;

        // whether an imperative write has overridden the binding (AD-24)
        bool m_overridden = false;

        // whether a declarative binding is installed at all (§7.3)
        bool m_has_binding = false;
    };

    template <typename T>
    const T& Property<T>::resolved() const {
        // an imperative write outranks everything, because it is the most recent decision of the caller (AD-24)
        if (m_overridden)
            return m_override_value;
        // an active animation writes through its own channel, which yields to an imperative override (AD-04d)
        if (m_presence.active)
            return m_animation_value;
        // a live binding produces the presented value, because no override or animation pre-empted it (§7.3)
        if (m_has_binding)
            return m_bound_value;
        // a detached property presents the value it was constructed with (§7.1)
        return m_value;
    }
    template <typename T>
    const T& Property<T>::get() const {
        // read the value the three channels resolve to (§7.1, AD-24, AD-04d)
        return resolved();
    }
    template <typename T>
    void Property<T>::set(T new_value) {
        // the imperative value is recorded separately, so a later animation yields to it (AD-24, AD-04d)
        m_override_value = std::move(new_value);
        // an imperative write breaks the declarative binding until rebind reinstates it (AD-24)
        m_overridden = true;
        // an animation keeps its channel, so it yields cleanly rather than being erased (AD-04d)
        if (m_engine != nullptr)
            m_engine->notify_changed(m_id);
    }
    template <typename T>
    void Property<T>::rebind() {
        // a property with no binding has nothing to reinstate, so its value is left alone (§7.3)
        if (m_engine == nullptr || m_binding == 0)
            return;
        // the imperative override is dropped, which hands the value back to the binding (AD-24)
        m_overridden = false;
        // the binding is marked dirty, so the next settle recomputes it from its current sources (§7.3)
        m_engine->invalidate(m_binding);
    }
    template <typename T>
    void Property<T>::bind(BindingFn<T> evaluator, std::vector<PropertyId> dependencies) {
        // a detached property cannot bind, so the caller must attach an engine first (§7.2)
        if (m_engine == nullptr)
            return;
        // the bound channel starts at the presented value, so a binding that never writes leaves it intact (§7.3)
        m_bound_value = resolved();
        // the evaluator captures the property, so it writes through the bound channel only (§7.3)
        m_binding = m_engine->bind(m_id, std::move(dependencies), [this, evaluator] {
            // a binding body with no evaluator leaves the value untouched (§7.3)
            if (evaluator)
                write_bound(evaluator());
        });
        // a fresh binding is live again, because binding reinstates the declarative channel (AD-24)
        m_has_binding = true;
        m_overridden = false;
    }
    template <typename T>
    void Property<T>::bind_static(StaticEvaluator evaluator, void* context, std::span<const PropertyId> dependencies) {
        // a detached property cannot bind, so the caller must attach an engine first (§7.2)
        if (m_engine == nullptr)
            return;
        // the bound channel starts at the presented value, so a binding that never writes leaves it intact (§7.3)
        m_bound_value = resolved();
        // the plain function and its context are remembered here, because the engine stores no callable for them (§13.3)
        m_static_evaluator = evaluator;
        m_static_context = context;
        // the engine receives a thunk over this property, which is what keeps the path free of captures (§13.3)
        m_binding = m_engine->bind_static(m_id, dependencies, &Property<T>::evaluate_static, this);
        // a fresh binding is live again, because binding reinstates the declarative channel (AD-24)
        m_has_binding = true;
        m_overridden = false;
    }
    template <typename T>
    void Property<T>::evaluate_static(void* context) {
        // the property is recovered from the context, which is what keeps the path allocation free (§13.3)
        auto* self = static_cast<Property<T>*>(context);
        // a binding body with no evaluator leaves the value untouched (§7.3)
        if (self->m_static_evaluator != nullptr)
            self->write_bound(self->m_static_evaluator(self->m_static_context));
    }
    template <typename T>
    void Property<T>::unbind() {
        // a detached property has nothing to remove (§7.3)
        if (m_engine == nullptr || m_binding == 0)
            return;
        // the engine forgets the evaluator, so a later change no longer reaches the property (§7.3)
        m_engine->unbind(m_binding);
        m_binding = 0;
        // the last bound value is promoted to the presented value, so unbinding freezes rather than blanks (§7.3)
        m_value = m_bound_value;
        // the property is a plain value again, which is what unbinding means (§7.3)
        m_has_binding = false;
    }
    template <typename T>
    PropertyId Property<T>::id() const {
        // the identifier the graph keys on, which is invalid until a component assigns it (§7.2)
        return m_id;
    }
    template <typename T>
    void Property<T>::set_id(PropertyId id) {
        // a component assigns the identifier when it registers the property (§7.2)
        m_id = id;
    }
    template <typename T>
    void Property<T>::set_engine(BindingEngine* engine) {
        // a component attaches the engine once, before it binds any property (§7.2)
        m_engine = engine;
    }
    template <typename T>
    bool Property<T>::bound() const {
        // an overridden property is no longer driven by its binding, so it is not bound (AD-24)
        return m_has_binding && !m_overridden;
    }
    template <typename T>
    bool Property<T>::overridden() const {
        // report whether an imperative write owns the presented value (AD-24)
        return m_overridden;
    }
    template <typename T>
    void Property<T>::animate(T value) {
        // the animation writes its own channel, which never overwrites the user value (AD-04d)
        m_animation_value = std::move(value);
        // the presence record opens, which is what makes the channel authoritative (§7.3, AD-04d)
        m_presence.active = true;
        m_presence.frames++;
        // consumers of the property see the animated value, so the graph is marked (§7.3)
        if (m_engine != nullptr)
            m_engine->notify_changed(m_id);
    }
    template <typename T>
    const AnimationPresence& Property<T>::presence() const {
        // the presence record reports whether the animation channel runs (§7.3, AD-04d)
        return m_presence;
    }
    template <typename T>
    void Property<T>::end_animation() {
        // an absent animation is already closed, so the call changes nothing (§7.3)
        if (!m_presence.active)
            return;
        // the channel closes, which hands the value back to the override or the binding (AD-04d)
        m_presence.active = false;
        // consumers see the restored value, so the graph is marked (§7.3)
        if (m_engine != nullptr)
            m_engine->notify_changed(m_id);
    }
    template <typename T>
    void Property<T>::write_bound(T value) {
        // the bound value is recorded separately, so an override or an animation can mask it (AD-24, AD-04d)
        m_bound_value = std::move(value);
    }
} // namespace albor
