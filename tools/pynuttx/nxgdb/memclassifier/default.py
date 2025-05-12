############################################################################
# tools/pynuttx/nxgdb/memclassifier/default.py
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

from nxgdb.memclassifier.utils import (
    backtrace_function_name_contain,
    backtrace_function_name_equal,
    backtrace_function_name_startswith,
)


def is_quickjs(mb):
    for funcname, filename in mb.backtrace():
        if funcname in ["js_def_malloc", "js_def_realloc"]:
            return True
    return False


def is_graphic(mb):
    for funcname, filename in mb.backtrace():
        if funcname in [
            "ft_alloc",
            "ft_realloc",
            "FT_Stream_New",
            "lv_mem_realloc",
            "lv_mem_alloc",
            "lv_malloc",
            "lv_realloc",
            "lv_malloc_zeroed",
            "gui_context_init(bool)",
            "gui_create_widget(void*, char const*)",
            "ferry::WidgetDiv::setStyle(int, char const*)",
            "ferry::WidgetIMP::onInit()",
            "gui_get_taskinfo()",
            "ferry::DomElement::refreshingWidgetStyle()",
            "gui_create_context(bool)",
            "ferry::DomEntity::updateWidgetChildren(ferry::DomEntity**)",
            "ferry::DomElement::setAttr(ferry::ItemKeyValue const*)",
        ]:
            return True

    return False


def is_freetype_cache(mb):
    for funcname, filename in mb.backtrace():
        if funcname in [
            "freetype_image_create_cb",
        ]:
            return True
    return False


def is_image_cache(mb):
    for funcname, filename in mb.backtrace():
        if funcname in [
            "freetype_image_create_cb",
        ]:
            return True
    return False


def is_style(mb):
    for funcname, filename in mb.backtrace():
        if any(funcname.startswith(func) for func in ["ferry::style::"]):
            return True
    return False


categories = {
    "quickjs": is_quickjs,
    "graphic": {
        "judger": is_graphic,
        "subcategories": {
            "lv_malloc": backtrace_function_name_equal(
                ["lv_malloc_core", "lv_realloc_core"]
            ),
            "ft_realloc ": backtrace_function_name_equal(["ft_realloc "]),
            "freetype_cache": {"judger": is_freetype_cache},
            "freetype": backtrace_function_name_equal(["ft_alloc"]),
            "font": backtrace_function_name_equal(["ferry::Font::"]),
            "textBase": backtrace_function_name_startswith(["ferry::TextBase::"]),
            "image_cache": {"judger": is_image_cache},
            "widget": backtrace_function_name_startswith(["ferry::Widget"]),
            "string_with_Kid": backtrace_function_name_startswith(
                ["ferry::stringToKidInitialize", "ferry::kidToStringInitialize"]
            ),
            "ImageCacheManager": backtrace_function_name_startswith(
                ["ferry::ImageCacheManager::", "gui_init_image_cachemanager"]
            ),
            "ImageProvider": backtrace_function_name_startswith(
                ["ferry::ImageProvider::"]
            ),
            "Yoga": backtrace_function_name_startswith(["YGNode", "facebook::yoga::"]),
            "onPreset": backtrace_function_name_startswith(
                ["ferry::WidgetIMP::onPreSet"]
            ),
        },
    },
    "style": {
        "judger": backtrace_function_name_startswith(
            ["ferry::style::", "ferry::DomElement::computeStyle"]
        ),
        "subcategories": {
            "StrRefHeader": backtrace_function_name_startswith(
                ["ferry::style::StrRefHeader::From"]
            ),
            "computeStyle": backtrace_function_name_contain(["computeStyle"]),
            "StringPool": backtrace_function_name_startswith(
                ["ferry::style::StringPool::StringPool"]
            ),
            "selector": backtrace_function_name_startswith(
                ["ferry::style::StyleManagerBuilder::addSelector"]
            ),
            "StyleManager": backtrace_function_name_startswith(
                ["ferry::style::StyleManager::StyleManager"]
            ),
        },
    },
    "libuv": backtrace_function_name_equal(["uv__realloc", "uv_pipe", "uv_loop_init"]),
    "dom": {
        "judger": backtrace_function_name_startswith(["js_dom_create", "ferry::PbDom"]),
        "subcategories": {
            "create_element_from_js": backtrace_function_name_startswith(
                ["create_element_from_js", "js_dom_create_element"]
            ),
        },
    },
    "curl": backtrace_function_name_startswith(["Curl_"]),
    "native_proxy": backtrace_function_name_startswith(
        [
            "AIOTJS::js_nativeproxy_get",
            "AIOTJS::ComponentCallHook::pauseTracking",
        ]
    ),
    "rapidjson": backtrace_function_name_startswith(
        ["rapidjson::ParseResult", "rapidjson::GenericReader"]
    ),
    "feature_require": backtrace_function_name_contain(
        ["ferry::__app_require", "_onRequired"]
    ),
    "bundle": backtrace_function_name_startswith(["ferry::ZipFile::ZipFile"]),
    "binder": backtrace_function_name_equal(
        ["binder_mmap", "android::RefBase::RefBase()"]
    ),
    "stack": {
        "judger": backtrace_function_name_startswith(["up_create_stack"]),
        "ignore": True,
    },
    "framework": backtrace_function_name_startswith(
        [
            "ferry::Framework::",
            "ferry::Application::",
            "ferry::__runApplication",
            "ferry::PageLoader::createPage(ferry::PageInfo const&)",
            "ferry::PageNavigator::",
            "ferry::Page::init",
            "os::app::ApplicationThread::ApplicationThread(os::app::Application*)",
            "ferry::AppLoader::initAppEnv(ferry::Application*",
        ]
    ),
    "other": lambda mb: True,
}
