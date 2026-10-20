import os
import sys
import xml.etree.ElementTree as ET


def child_id(element, tag, name):
    value = element.find("./%s[@name='%s']" % (tag, name)) if element is not None else None
    return value.text if value is not None else None


def main():
    if len(sys.argv) != 2:
        raise RuntimeError("usage: analyze_editor_viewport_composition_renderdoc.py <capture.xml>")
    xml_path = os.path.abspath(sys.argv[1])
    chunks = ET.parse(xml_path).getroot().findall(".//chunk")

    images = {}
    views = {}
    descriptor_views = {}
    for chunk in chunks:
        name = chunk.get("name", "")
        if name == "vkCreateImage":
            create_info = chunk.find("./struct[@name='CreateInfo']")
            extent = create_info.find("./struct[@name='extent']") if create_info is not None else None
            image_id = child_id(chunk, "ResourceId", "Image")
            format_element = create_info.find("./enum[@name='format']") if create_info is not None else None
            image_format = format_element.get("string") if format_element is not None else None
            if extent is not None and image_id is not None:
                images[image_id] = {
                    "format": image_format,
                    "width": child_id(extent, "uint", "width"),
                    "height": child_id(extent, "uint", "height"),
                }
        elif name == "vkCreateImageView":
            create_info = chunk.find("./struct[@name='CreateInfo']")
            image_id = child_id(create_info, "ResourceId", "image")
            view_id = child_id(chunk, "ResourceId", "View")
            if image_id is not None and view_id is not None:
                views[view_id] = image_id
        elif name == "Internal::Initial Contents":
            resource = chunk.find("./ResourceId[@name='id'][@typename='VkDescriptorSet']")
            if resource is None:
                continue
            descriptor_views[resource.text] = [
                slot.findtext("./ResourceId[@name='resource']")
                for slot in chunk.findall("./array[@name='Bindings']/struct")
                if slot.find("./ResourceId[@name='resource']") is not None
            ]

    indexed_draws = [chunk for chunk in chunks if chunk.get("name") == "vkCmdDrawIndexed"]
    viewport_samples = []
    for index, chunk in enumerate(chunks):
        if chunk.get("name") != "vkCmdBindDescriptorSets":
            continue
        if child_id(chunk, "uint", "firstSet") != "1":
            continue
        command_buffer = child_id(chunk, "ResourceId", "commandBuffer")
        bound_sets = [entry.text for entry in chunk.findall("./array[@name='pDescriptorSets']/ResourceId")]
        for set_id in bound_sets:
            for view_id in descriptor_views.get(set_id, []):
                image_id = views.get(view_id)
                image = images.get(image_id)
                if not image or image["format"] != "VK_FORMAT_R16G16B16A16_SFLOAT":
                    continue
                draw = next((
                    action for action in chunks[index + 1:index + 5]
                    if action.get("name") == "vkCmdDrawIndexed"
                    and child_id(action, "ResourceId", "commandBuffer") == command_buffer
                ), None)
                if draw is not None:
                    viewport_samples.append((chunk, draw, set_id, image_id, image))

    if not viewport_samples:
        raise RuntimeError("no UI descriptor set sampled the RGBA16F viewport target before an indexed draw")
    bind, draw, set_id, image_id, image = viewport_samples[0]
    report_path = os.path.splitext(xml_path)[0] + "_composition_analysis.txt"
    with open(report_path, "w", encoding="ascii") as report:
        report.write("capture_xml=%s\n" % xml_path)
        report.write("viewport_target_image=%s format=%s extent=%sx%s\n" % (
            image_id, image["format"], image["width"], image["height"]
        ))
        report.write("external_descriptor_set=%s first_set=1 bind_chunk=%s draw_chunk=%s\n" % (
            set_id, bind.get("chunkIndex"), draw.get("chunkIndex")
        ))
        report.write("ui_indexed_draws=%d\n" % len(indexed_draws))
        report.write("structural_result=PASS\n")
    with open(report_path, "r", encoding="ascii") as report:
        print(report.read(), end="")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
