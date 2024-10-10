local library = floe.new_library({
    name = "Test Lua",
    tagline = "Tagline",
    author = "Tester",
    background_image_path = "images/background.jpg",
    icon_image_path = "images/icon.png",
    minor_version = 2,
})

-- ================================================================================
local single_sample = floe.new_instrument(library, {
    name = "Single Sample",
    folders = "Folder",
    description = "Description",
    tags = {},
    waveform_audio_path = "Samples/a.flac",
})

floe.add_region(single_sample, {
    file = {
        root_key = 60,
        path = "Samples/a.flac",
    },
    trigger_criteria = {
        trigger_event = "note-on",
        key_range = { 0, 128 },
        velocity_range = { 0, 100 },
    },
})

-- ================================================================================
local same_sample_twice = floe.new_instrument(library, {
    name = "Same Sample Twice",
    folders = "Folder",
    description = "Description",
    tags = {},
    waveform_audio_path = "Samples/a.flac",
})

floe.add_region(same_sample_twice, {
    file = {
        root_key = 30,
        path = "Samples/a.flac",
    },
    trigger_criteria = {
        trigger_event = "note-on",
        key_range = { 0, 60 },
        velocity_range = { 0, 100 },
    },
})
floe.add_region(same_sample_twice, {
    file = {
        root_key = 60,
        path = "Samples/a.flac",
    },
    trigger_criteria = {
        trigger_event = "note-on",
        key_range = { 60, 128 },
        velocity_range = { 0, 100 },
    },
})

-- ================================================================================
local auto_mapped_samples = floe.new_instrument(library, {
    name = "Auto Mapped Samples",
    folders = "Folder",
    description = "Description",
    tags = {},
    waveform_audio_path = "Samples/a.flac",
})
local auto_map_config = {
    {
        file = 'a',
        root = 20,
    },
    {
        file = 'b',
        root = 40,
    },
    {
        file = 'c',
        root = 60,
    },
    {
        file = 'd',
        root = 80,
    },
}
for _, config in pairs(auto_map_config) do
    floe.add_region(auto_mapped_samples, {
        file = {
            root_key = config.root,
            path = "Samples/" .. config.file .. ".flac",
        },
        trigger_criteria = {
            trigger_event = "note-on",
            velocity_range = { 0, 100 },
        },
        options = {
            auto_map_key_range_group = "group1",
        },
    })
end

return library
