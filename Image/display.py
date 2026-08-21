import os


def get_images(path):
    images = {"detections": [], "originals": []}
    for file in os.listdir(path):
        if file.endswith((".jpg", ".png", ".jpeg")):
            if "original" in file:
                images["originals"].append(file)
            else:
                images["detections"].append(file)
    return images


def create_html_content(images):
    html = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Image Detections</title>
    <style>
        body { font-family: sans-serif; margin: 20px; background-color: #f4f4f4; }
        h1 { text-align: center; color: #333; }
        h2 { color: #555; border-bottom: 2px solid #ddd; padding-bottom: 10px; }
        .container {
            display: flex;
            flex-wrap: wrap;
            justify-content: flex-start;
            gap: 20px;
        }
        .image-box {
            border: 1px solid #ccc;
            border-radius: 8px;
            padding: 15px;
            background-color: #fff;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            text-align: center;
            width: 400px;
        }
        .image-box h3 { margin-top: 0; font-size: 1em; color: #666; }
        img {
            max-width: 100%;
            height: auto;
            border-radius: 4px;
        }
    </style>
</head>
<body>
    <h1>Image Viewer</h1>
"""

    for category, image_files in images.items():
        if image_files:
            html += f'<h2>{category.capitalize()}</h2>\n<div class="container">\n'
            for image_file in sorted(image_files):
                html += f"""
                <div class="image-box">
                    <h3>{image_file}</h3>
                    <img src=\"detections/{image_file}\" alt=\"{image_file}\">
                </div>"""
            html += "\n</div>\n"
        else:
            html += f'<h2>{category.capitalize()}</h2>\n<div class="container"><p style="color:#888;">No images yet. Run a detection to see results here.</p></div>\n'

    html += """
</body>
</html>
"""
    return html


if __name__ == "__main__":
    detections_path = "detections"
    images = get_images(detections_path)
    html_content = create_html_content(images)
    with open("index.html", "w") as f:
        f.write(html_content)
    print("index.html generated successfully.")

