#!/usr/bin/env python3
"""Instrument the pinned ESP-DL constructor in the build directory only.

The upstream constructor hides load's esp_err_t and build ignores alloc's bool.
Keep managed_components untouched; refuse a different upstream revision.
"""
import hashlib
from pathlib import Path
import sys

UPSTREAM_SHA256 = "5dd0c9e03e81ea6ed993d8fff1554b09f5f4e7538a57722971ddaa31f53404a6"


def instrument(source: bytes) -> str:
    if hashlib.sha256(source).hexdigest() != UPSTREAM_SHA256:
        raise ValueError("ESP-DL model source differs from pinned 3.3.14; review diagnostics patch")
    text = source.decode()
    before = """    if (this->load(rodata_address_or_partition_label_or_path, location, key, param_copy) == ESP_OK) {
        this->build(max_internal_size, mm_type, false, input_shapes);
    }"""
    after = """    const esp_err_t load_result = this->load(rodata_address_or_partition_label_or_path, location, key, param_copy);
    ESP_LOGI(TAG, "init/load returned %s (0x%x)", esp_err_to_name(load_result), (unsigned)load_result);
    if (load_result == ESP_OK) {
        this->build(max_internal_size, mm_type, false, input_shapes);
    }"""
    if text.count(before) != 1:
        raise ValueError("Unexpected constructor")
    text = text.replace(before, after)
    before = "    memory_manager->alloc(m_fbs_model, m_execution_plan, m_model_context, input_shapes);"
    after = """    const bool allocated = memory_manager->alloc(m_fbs_model, m_execution_plan, m_model_context, input_shapes);
    ESP_LOGI(TAG, "init/arena allocated=%s; internal budget=%u bytes", allocated ? "true" : "false", (unsigned)max_internal_size);
    if (!allocated) {
        // Do not expose unallocated tensors as a successful graph.
        m_inputs.clear();
        m_outputs.clear();
        m_fbs_model->clear_map();
        delete memory_manager;
        return;
    }"""
    if text.count(before) != 1:
        raise ValueError("Unexpected memory planner")
    return text.replace(before, after)


if __name__ == "__main__":
    Path(sys.argv[2]).write_text(instrument(Path(sys.argv[1]).read_bytes()))
