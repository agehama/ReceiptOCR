#pragma once
#include <Siv3D.hpp> // Siv3D v0.6.15

#ifdef _DEBUG
constexpr auto VisionExePath = U"../../CloudVision/bin/Debug/net8.0/CloudVision.exe";
#else
constexpr auto VisionExePath = U"../../CloudVision/bin/Release/net8.0/CloudVision.exe";
#endif

struct TextAnnotation
{
	Array<Vec2> BoundingPoly;
	RectF BoundingBox;
	String Description;

	Vec2 center() const
	{
		if (BoundingPoly.empty())
		{
			return Vec2::Zero();
		}

		Vec2 sum = Vec2::Zero();
		for (const auto& v : BoundingPoly)
		{
			sum += v;
		}
		return sum / BoundingPoly.size();
	}

	Vec2 minMaxY() const
	{
		if (BoundingPoly.empty())
		{
			return Vec2::Zero();
		}

		double minY = DBL_MAX;
		double maxY = -DBL_MAX;
		for (const auto& v : BoundingPoly)
		{
			minY = std::min(minY, v.y);
			maxY = std::max(maxY, v.y);
		}

		return Vec2(minY, maxY);
	}

	Vec2 minMaxX() const
	{
		if (BoundingPoly.empty())
		{
			return Vec2::Zero();
		}

		double minX = DBL_MAX;
		double maxX = -DBL_MAX;
		for (const auto& v : BoundingPoly)
		{
			minX = std::min(minX, v.x);
			maxX = std::max(maxX, v.x);
		}

		return Vec2(minX, maxX);
	}

	void calc()
	{
		double minX = DBL_MAX;
		double minY = DBL_MAX;
		double maxX = -DBL_MAX;
		double maxY = -DBL_MAX;
		for (const auto& v : BoundingPoly)
		{
			minX = std::min(minX, v.x);
			minY = std::min(minY, v.y);
			maxX = std::max(maxX, v.x);
			maxY = std::max(maxY, v.y);
		}
		BoundingBox = RectF(minX, minY, maxX - minX, maxY - minY).stretched(3, 30);
	}
};

inline Array<TextAnnotation> ReadResult(std::istream& is)
{
	Array<TextAnnotation> result;

	std::string line;

	auto readInt = [&line](std::istream& is)->Optional<int>
		{
			std::getline(is, line);
			if (line.ends_with('\r'))
			{
				line.pop_back();
			}
			const auto str = Unicode::FromUTF8(line);

			return ParseIntOpt<int>(str);
		}
	;

	auto readText = [&line](std::istream& is)->String
		{
			std::getline(is, line);
			if (line.ends_with('\r'))
			{
				line.pop_back();
			}
			return Unicode::FromUTF8(line);
		}
	;

	const auto wordCount = readInt(is);
	for (auto wo : step(wordCount.value()))
	{
		const auto wordVsCount = readInt(is);
		Array<Vec2> wordBBVs;
		for (auto v : step(wordVsCount.value()))
		{
			const auto xOpt = readInt(is);
			const auto yOpt = readInt(is);
			wordBBVs.emplace_back(xOpt.value(), yOpt.value());
		}

		TextAnnotation annotation;

		const auto text = readText(is);
		annotation.Description = text;
		annotation.BoundingPoly = wordBBVs;
		annotation.calc();
		result.push_back(annotation);
	}

	return result;
}
