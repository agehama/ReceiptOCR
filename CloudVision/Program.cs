using System;
using System.Linq;
using System.Text;
using Google.Cloud.Vision.V1;

var utf32 = Encoding.UTF32;

var bytes = utf32.GetBytes(args[0]);
var filepath = utf32.GetString(bytes);

var client = ImageAnnotatorClient.Create();

var feature = new Feature
{
    Type = Feature.Types.Type.TextDetection,
    MaxResults = 100,
};
var request = new AnnotateImageRequest()
{
    Image = Image.FromFile(filepath),
    Features = { feature },
};
var response = client.Annotate(request);

Console.WriteLine(response.FullTextAnnotation.Pages.Count);
foreach (var page in response.FullTextAnnotation.Pages)
{
    Console.WriteLine(page.Blocks.Count);
    foreach (var block in page.Blocks)
    {
        var blockVs = block.BoundingBox.Vertices;
        Console.WriteLine(blockVs.Count);
        foreach (var v in blockVs)
        {
            Console.WriteLine(v.X);
            Console.WriteLine(v.Y);
        }

        Console.WriteLine(block.Paragraphs.Count);
        foreach (var paragraph in block.Paragraphs)
        {
            var paragraphVs = paragraph.BoundingBox.Vertices;
            Console.WriteLine(paragraphVs.Count);
            foreach (var v in paragraphVs)
            {
                Console.WriteLine(v.X);
                Console.WriteLine(v.Y);
            }

            Console.WriteLine(paragraph.Words.Count);
            foreach (var word in paragraph.Words)
            {
                var wordVs = word.BoundingBox.Vertices;
                Console.WriteLine(wordVs.Count);
                foreach (var v in wordVs)
                {
                    Console.WriteLine(v.X);
                    Console.WriteLine(v.Y);
                }

                var chars = word.Symbols.SelectMany(symbol => symbol.Text);
                Console.WriteLine(new string(chars.ToArray()));
            }
        }
    }
}
