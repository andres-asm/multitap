#include "libretro_options.h"

#include <cstring>
#include <new>

#include "host_log.h"

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

// nullptr in, nullptr out. Returns false only if the allocation failed.
static bool copy_string(char** destination, const char* source)
{
    *destination = nullptr;
    if (source == nullptr)
    {
        return true;
    }
    size_t length = std::strlen(source);
    char* copy = new (std::nothrow) char[length + 1];
    if (copy == nullptr)
    {
        return false;
    }
    std::memcpy(copy, source, length + 1);
    *destination = copy;
    return true;
}

static bool same_text(const char* a, const char* b)
{
    return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}

// ---------------------------------------------------------------------------
// Freeing
// ---------------------------------------------------------------------------

static void free_option(LibretroOption* option)
{
    delete[] option->key;
    delete[] option->label;
    delete[] option->label_in_category;
    delete[] option->info;
    delete[] option->category_key;
    for (uint32_t i = 0; i < option->value_count; i++)
    {
        delete[] option->values[i];
        delete[] option->value_labels[i];
    }
    delete[] option->values;
    delete[] option->value_labels;
    *option = LibretroOption{};
}

static void free_lists(LibretroOption* list, uint32_t count, LibretroOptionCategory* categories, uint32_t category_count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        free_option(&list[i]);
    }
    delete[] list;
    for (uint32_t i = 0; i < category_count; i++)
    {
        delete[] categories[i].key;
        delete[] categories[i].label;
        delete[] categories[i].info;
    }
    delete[] categories;
}

static void free_saved(LibretroOptions* options);

void libretro_options_free(LibretroOptions* options)
{
    free_lists(options->options, options->count, options->categories, options->category_count);
    free_saved(options);
    *options = LibretroOptions{};
}

// ---------------------------------------------------------------------------
// One format for all three APIs
// ---------------------------------------------------------------------------

// What every API boils down to, still pointing at the core's (or our scratch) memory.
struct OptionSource
{
    const char*                   key               = nullptr;
    const char*                   label             = nullptr;
    const char*                   label_in_category = nullptr;
    const char*                   info              = nullptr;
    const char*                   category_key      = nullptr;
    const retro_core_option_value* values           = nullptr;   // up to RETRO_NUM_CORE_OPTION_VALUES_MAX, ends at value == nullptr
    const char*                   default_value     = nullptr;
};

static uint32_t count_values(const retro_core_option_value* values)
{
    uint32_t count = 0;
    while (count < RETRO_NUM_CORE_OPTION_VALUES_MAX && values[count].value != nullptr)
    {
        count++;
    }
    return count;
}

// Copies one source into `option`. Returns false on a bad definition (logged) or no memory.
static bool build_option(LibretroOption* option, const OptionSource* source)
{
    uint32_t value_count = count_values(source->values);
    if (value_count == 0)
    {
        host_log(LogLevel::warning, "Core option %s has no values; ignored", source->key);
        return false;
    }

    option->values = new (std::nothrow) char*[value_count]();
    option->value_labels = new (std::nothrow) char*[value_count]();
    if (option->values == nullptr || option->value_labels == nullptr)
    {
        delete[] option->values;
        delete[] option->value_labels;
        option->values = nullptr;
        option->value_labels = nullptr;
        return false;
    }
    option->value_count = value_count;

    bool ok = copy_string(&option->key, source->key) &&
              copy_string(&option->label, source->label != nullptr ? source->label : source->key) &&
              copy_string(&option->label_in_category, source->label_in_category) &&
              copy_string(&option->info, source->info) &&
              copy_string(&option->category_key, source->category_key);
    for (uint32_t i = 0; ok && i < value_count; i++)
    {
        ok = copy_string(&option->values[i], source->values[i].value) &&
             copy_string(&option->value_labels[i], source->values[i].label);
    }
    if (!ok)
    {
        return false;
    }

    // A default that isn't one of the values: use the first, like other frontends do.
    option->default_index = 0;
    bool default_found = false;
    for (uint32_t i = 0; i < value_count; i++)
    {
        if (same_text(option->values[i], source->default_value))
        {
            option->default_index = i;
            default_found = true;
            break;
        }
    }
    if (!default_found && source->default_value != nullptr)
    {
        host_log(LogLevel::warning, "Core option %s: default '%s' is not one of its values; using '%s'",
                 option->key, source->default_value, option->values[0]);
    }
    option->current_index = option->default_index;
    option->visible = true;
    return true;
}

// Keeps the user's choice across a re-registration when the key and value still exist.
// An option declared again keeps its value and visibility. Returns false for
// a new option.
static bool carry_over_value(LibretroOption* option, const LibretroOptions* previous)
{
    const LibretroOption* old_option = libretro_options_find(previous, option->key);
    if (old_option == nullptr)
    {
        return false;
    }
    const char* old_value = old_option->values[old_option->current_index];
    for (uint32_t i = 0; i < option->value_count; i++)
    {
        if (same_text(option->values[i], old_value))
        {
            option->current_index = i;
            break;
        }
    }
    option->visible = old_option->visible;
    return true;
}

// A new option takes the frontend's saved value, when there is a valid one.
static void apply_saved_value(LibretroOption* option, const LibretroOptions* options)
{
    for (uint32_t i = 0; i < options->saved_count; i++)
    {
        if (!same_text(options->saved_keys[i], option->key))
        {
            continue;
        }
        for (uint32_t v = 0; v < option->value_count; v++)
        {
            if (same_text(option->values[v], options->saved_values[i]))
            {
                option->current_index = v;
                return;
            }
        }
        host_log(LogLevel::warning, "Saved value \"%s\" for core option %s is not one of its values; using the default",
                 options->saved_values[i], option->key);
        return;
    }
}

static void free_saved(LibretroOptions* options)
{
    for (uint32_t i = 0; i < options->saved_count; i++)
    {
        delete[] options->saved_keys[i];
        delete[] options->saved_values[i];
    }
    delete[] options->saved_keys;
    delete[] options->saved_values;
    options->saved_keys = nullptr;
    options->saved_values = nullptr;
    options->saved_count = 0;
}

bool libretro_options_set_saved(LibretroOptions* options, const LibretroOptionValue* values, uint32_t count)
{
    free_saved(options);
    if (count == 0)
    {
        return true;
    }
    options->saved_keys = new (std::nothrow) char*[count]();
    options->saved_values = new (std::nothrow) char*[count]();
    if (options->saved_keys == nullptr || options->saved_values == nullptr)
    {
        delete[] options->saved_keys;
        delete[] options->saved_values;
        options->saved_keys = nullptr;
        options->saved_values = nullptr;
        return false;
    }
    options->saved_count = count;
    for (uint32_t i = 0; i < count; i++)
    {
        if (!copy_string(&options->saved_keys[i], values[i].key != nullptr ? values[i].key : "") ||
            !copy_string(&options->saved_values[i], values[i].value != nullptr ? values[i].value : ""))
        {
            free_saved(options);
            return false;
        }
    }
    return true;
}

// Builds a complete new list from `sources`, then swaps it in. On failure the old list stays.
static bool install(LibretroOptions* options, const OptionSource* sources, uint32_t source_count,
                    const retro_core_option_v2_category* categories, uint32_t api)
{
    uint32_t category_count = 0;
    while (categories != nullptr && categories[category_count].key != nullptr)
    {
        category_count++;
    }

    LibretroOption* list = source_count > 0 ? new (std::nothrow) LibretroOption[source_count] : nullptr;
    LibretroOptionCategory* category_list =
        category_count > 0 ? new (std::nothrow) LibretroOptionCategory[category_count] : nullptr;
    if ((source_count > 0 && list == nullptr) || (category_count > 0 && category_list == nullptr))
    {
        delete[] list;
        delete[] category_list;
        host_log(LogLevel::error, "Out of memory storing %u core options", source_count);
        return false;
    }

    // Bad definitions are skipped (logged), not fatal: the rest still works.
    uint32_t count = 0;
    for (uint32_t i = 0; i < source_count; i++)
    {
        bool duplicate = false;
        for (uint32_t j = 0; j < count; j++)
        {
            if (same_text(list[j].key, sources[i].key))
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            host_log(LogLevel::warning, "Core option %s is defined twice; the second one is ignored", sources[i].key);
            continue;
        }
        if (!build_option(&list[count], &sources[i]))
        {
            free_option(&list[count]);
            continue;
        }
        if (!carry_over_value(&list[count], options))
        {
            apply_saved_value(&list[count], options);
        }
        count++;
    }

    bool ok = true;
    for (uint32_t i = 0; i < category_count; i++)
    {
        ok = ok && copy_string(&category_list[i].key, categories[i].key) &&
             copy_string(&category_list[i].label, categories[i].desc != nullptr ? categories[i].desc : categories[i].key) &&
             copy_string(&category_list[i].info, categories[i].info);
    }
    if (!ok)
    {
        free_lists(list, count, category_list, category_count);
        host_log(LogLevel::error, "Out of memory storing core option categories");
        return false;
    }

    // Swap in. The core will read values on its own; a fresh list isn't an "update".
    free_lists(options->options, options->count, options->categories, options->category_count);
    options->options = list;
    options->count = count;
    options->categories = category_list;
    options->category_count = category_count;
    options->api_used = api;
    options->serial++;
    return true;
}

// ---------------------------------------------------------------------------
// v2 and v1: already structured
// ---------------------------------------------------------------------------

bool libretro_options_register_v2(LibretroOptions* options, const retro_core_options_v2* definitions)
{
    uint32_t count = 0;
    while (definitions->definitions != nullptr && definitions->definitions[count].key != nullptr)
    {
        count++;
    }

    OptionSource* sources = count > 0 ? new (std::nothrow) OptionSource[count] : nullptr;
    if (count > 0 && sources == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const retro_core_option_v2_definition* definition = &definitions->definitions[i];
        sources[i].key               = definition->key;
        sources[i].label             = definition->desc;
        sources[i].label_in_category = definition->desc_categorized;
        sources[i].info              = definition->info;   // info_categorized not used yet
        sources[i].category_key      = definition->category_key;
        sources[i].values            = definition->values;
        sources[i].default_value     = definition->default_value;
    }

    bool ok = install(options, sources, count, definitions->categories, 2);
    delete[] sources;
    return ok;
}

bool libretro_options_register_v1(LibretroOptions* options, const retro_core_option_definition* definitions)
{
    uint32_t count = 0;
    while (definitions[count].key != nullptr)
    {
        count++;
    }

    OptionSource* sources = count > 0 ? new (std::nothrow) OptionSource[count] : nullptr;
    if (count > 0 && sources == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        sources[i].key           = definitions[i].key;
        sources[i].label         = definitions[i].desc;
        sources[i].info          = definitions[i].info;
        sources[i].values        = definitions[i].values;
        sources[i].default_value = definitions[i].default_value;
    }

    bool ok = install(options, sources, count, nullptr, 1);
    delete[] sources;
    return ok;
}

// ---------------------------------------------------------------------------
// v0: "Label; first|second|third"
// ---------------------------------------------------------------------------

bool libretro_options_register_v0(LibretroOptions* options, const retro_variable* variables)
{
    uint32_t count = 0;
    while (variables[count].key != nullptr)
    {
        count++;
    }

    // Scratch: a writable copy of each "value" string (split in place) and a
    // value array per variable pointing into it. install() copies everything,
    // so all of this is freed at the end.
    OptionSource* sources = count > 0 ? new (std::nothrow) OptionSource[count] : nullptr;
    char** texts = count > 0 ? new (std::nothrow) char*[count]() : nullptr;
    retro_core_option_value* value_arrays =
        count > 0 ? new (std::nothrow) retro_core_option_value[static_cast<size_t>(count) * RETRO_NUM_CORE_OPTION_VALUES_MAX]()
                  : nullptr;
    bool ok = count == 0 || (sources != nullptr && texts != nullptr && value_arrays != nullptr);

    uint32_t used = 0;
    for (uint32_t i = 0; ok && i < count; i++)
    {
        const char* key = variables[i].key;
        const char* text = variables[i].value;
        const char* separator = text != nullptr ? std::strstr(text, "; ") : nullptr;
        if (separator == nullptr)
        {
            host_log(LogLevel::warning, "Core option %s: '%s' is not 'Label; value|value'; ignored", key,
                     text != nullptr ? text : "(null)");
            continue;
        }
        if (!copy_string(&texts[i], text))
        {
            ok = false;
            break;
        }

        char* label = texts[i];
        char* list = texts[i] + (separator - text);
        *list = '\0';      // ends the label at ';'
        list += 2;         // skips "; "

        retro_core_option_value* values = &value_arrays[static_cast<size_t>(used) * RETRO_NUM_CORE_OPTION_VALUES_MAX];
        uint32_t value_count = 0;
        char* cursor = list;
        while (*cursor != '\0' && value_count < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
        {
            char* bar = std::strchr(cursor, '|');
            if (bar != nullptr)
            {
                *bar = '\0';
            }
            if (*cursor != '\0')
            {
                values[value_count].value = cursor;
                value_count++;
            }
            if (bar == nullptr)
            {
                break;
            }
            cursor = bar + 1;
        }

        sources[used].key           = key;
        sources[used].label         = label;
        sources[used].values        = values;
        sources[used].default_value = value_count > 0 ? values[0].value : nullptr;   // v0: first = default
        used++;
    }

    if (ok)
    {
        ok = install(options, sources, used, nullptr, 0);
    }
    else
    {
        host_log(LogLevel::error, "Out of memory reading SET_VARIABLES");
    }

    for (uint32_t i = 0; texts != nullptr && i < count; i++)
    {
        delete[] texts[i];
    }
    delete[] texts;
    delete[] value_arrays;
    delete[] sources;
    return ok;
}

// ---------------------------------------------------------------------------
// Lookups and changes
// ---------------------------------------------------------------------------

const LibretroOption* libretro_options_find(const LibretroOptions* options, const char* key)
{
    if (key == nullptr)
    {
        return nullptr;
    }
    for (uint32_t i = 0; i < options->count; i++)
    {
        if (same_text(options->options[i].key, key))
        {
            return &options->options[i];
        }
    }
    return nullptr;
}

const char* libretro_options_get(const LibretroOptions* options, const char* key)
{
    const LibretroOption* option = libretro_options_find(options, key);
    return option != nullptr ? option->values[option->current_index] : nullptr;
}

bool libretro_options_set(LibretroOptions* options, const char* key, const char* value)
{
    // find() returns const for readers; here we own the list, so look it up directly.
    LibretroOption* option = nullptr;
    for (uint32_t i = 0; key != nullptr && i < options->count; i++)
    {
        if (same_text(options->options[i].key, key))
        {
            option = &options->options[i];
            break;
        }
    }
    if (option == nullptr)
    {
        host_log(LogLevel::warning, "Option %s: no such option", key != nullptr ? key : "(null)");
        return false;
    }

    for (uint32_t i = 0; i < option->value_count; i++)
    {
        if (same_text(option->values[i], value))
        {
            if (option->current_index != i)
            {
                option->current_index = i;
                options->updated = true;
                host_log(LogLevel::info, "Option %s = %s", option->key, option->values[i]);
            }
            return true;
        }
    }
    host_log(LogLevel::warning, "Option %s: '%s' is not one of its values", option->key,
             value != nullptr ? value : "(null)");
    return false;
}

bool libretro_options_set_visible(LibretroOptions* options, const char* key, bool visible)
{
    for (uint32_t i = 0; key != nullptr && i < options->count; i++)
    {
        if (same_text(options->options[i].key, key))
        {
            options->options[i].visible = visible;
            return true;
        }
    }
    return false;
}

bool libretro_options_take_update(LibretroOptions* options)
{
    bool updated = options->updated;
    options->updated = false;
    return updated;
}
