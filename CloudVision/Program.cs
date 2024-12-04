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

Console.WriteLine(response.TextAnnotations.Count - 1);
for (int i = 1; i < response.TextAnnotations.Count; i++)
{
    var annotation = response.TextAnnotations[i];
    var blockVs = annotation.BoundingPoly.Vertices;
    Console.WriteLine(blockVs.Count);
    foreach (var v in blockVs)
    {
        Console.WriteLine(v.X);
        Console.WriteLine(v.Y);
    }

    Console.WriteLine(annotation.Description);
}
