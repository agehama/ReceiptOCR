# include <Siv3D.hpp> // Siv3D v0.6.15

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

enum class MarkType
{
	Unassigned,
	ShopName,
	Date,
	Time,
	Goods,
	Price,
	Number,
	Ignore,
};

constexpr Color MarkColor[] =
{
	Color{ 204, 204, 204 },
	Palette::Orange,
	Palette::Magenta,
	Color{ 224, 34, 116 },
	Color{ 32, 214, 44 },
	Color{ 0, 153, 230 },
	Color{ 0, 153, 230 },
	Color{ 204, 204, 204 },
};

struct ReceiptData
{
	Vec2 topLeft;
	Polygon boundingPolygon;
	Image image;
	Texture texture;
	Array<Array<TextAnnotation>> textGroup;
	HashTable<Point, MarkType> textMarkType;
	HashSet<Point> updatedMarkIndices;
	Vec2 xAxis;
	Vec2 yAxis;
	int32 verticalSpacing = 0.0;

	double angle() const
	{
		return Math::Atan2(xAxis.y, xAxis.x);
	}

	void init()
	{
		// 全ブロックの縦方向と横方向の平均をそれぞれ取ったものを軸の方向とする
		{
			xAxis = yAxis = Vec2::Zero();
			for (const auto& [groupIndex, group] : Indexed(textGroup))
			{
				for (const auto& [textIndex, text] : Indexed(group))
				{
					const auto& p = text.BoundingPoly;
					xAxis += p[1] - p[0];
					yAxis += p[2] - p[1];
				}
			}

			xAxis.normalize();
			yAxis.normalize();
		}

		// ブロックの高さを整数に丸めた最頻値を行間幅とする
		{
			HashTable<int32, size_t> spacingCounts;
			for (const auto& [groupIndex, group] : Indexed(textGroup))
			{
				for (const auto& [textIndex, text] : Indexed(group))
				{
					const auto& p = text.BoundingPoly;
					const auto spacing = static_cast<int32>((p[2] - p[1]).length());
					++spacingCounts[spacing];
				}
			}

			auto it = std::max_element(spacingCounts.begin(), spacingCounts.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
			verticalSpacing = it->first;
		}

		// 二つのブロックA,Bの大小関係は行間幅より離れていればy座標、そうでなければx座標で比較する
		// 行間幅 < abs(A.y - B.y) ? A.y < B.y : A.x < B.x
		textGroup.sort_by([&](const Array<TextAnnotation>& a, const Array<TextAnnotation>& b)
			{
				// 各行の左上位置で比較する
				const auto aPos = a[0].BoundingPoly[0];
				const auto bPos = b[0].BoundingPoly[0];

				if (verticalSpacing < (aPos - bPos).dot(yAxis), true) // verticalSpacingの判定が微妙なのでやっぱり常にyだけで比較する
				{
					return aPos.dot(yAxis) < bPos.dot(yAxis);
				}
				else
				{
					return aPos.dot(xAxis) < bPos.dot(xAxis);
				}
			}
		);

		indexMap.clear();
		allText = U"";
		for (const auto& [groupIndex, group] : Indexed(textGroup))
		{
			for (const auto& [textIndex, text] : Indexed(group))
			{
				const Point index(groupIndex, textIndex);
				for (const auto& c : text.Description)
				{
					indexMap.push_back(index);
				}
				allText += text.Description;
			}
		}

		Console << allText;
		Console << U"";

		//Logger << U"input allText:";
		//Logger << allText;
		//Logger << U"";

		inferenceMark();
	}

private:

	String allText;
	//HashTable<size_t, Point> indexMap; // allTextの文字インデックス -> [group, text]のインデックス
	Array<Point> indexMap; // allTextの文字インデックス -> [group, text]のインデックス

	void inferenceMark()
	{
		textMarkType.clear();
		for (const auto& [groupIndex, group] : Indexed(textGroup))
		{
			for (const auto& [textIndex, text] : Indexed(group))
			{
				textMarkType[Point(groupIndex, textIndex)] = MarkType::Unassigned;
			}
		}

		checkNumber();
		checkDate();
		checkTime();
		checkShopName();
		checkPrice();
		checkItemName();

		checkIgnore();
	}

	void checkShopName()
	{
		const auto reg = UR"([a-zA-Z\p{Katakana}\p{Han}ーｰ\-～~^店]+店)"_re;
		const auto match = reg.search(allText);

		if (!match.isEmpty())
		{
			const StringView textView = allText;
			const auto matchedView = match[0].value();
			const auto beginIndex = &*matchedView.begin() - &*textView.begin();
			for (auto i : step(matchedView.size()))
			{
				const auto blockIndex = indexMap[beginIndex + i];
				textMarkType[blockIndex] = MarkType::ShopName;
			}
		}
	}

	void checkDate()
	{
		const auto reg = UR"(\d\d\d\d[年/]\d\d?[月/]\d\d?日?)"_re;
		const auto match = reg.search(allText);

		if (!match.isEmpty())
		{
			const StringView textView = allText;
			const auto matchedView = match[0].value();
			const auto beginIndex = &*matchedView.begin() - &*textView.begin();
			for (auto i : step(matchedView.size()))
			{
				const auto blockIndex = indexMap[beginIndex + i];
				textMarkType[blockIndex] = MarkType::Date;
			}

			// 商品が日付より前に来るケースは稀なので、手前で検出した金額は誤検出として戻しておく
			for (size_t i = 0; i < beginIndex; ++i)
			{
				const auto blockIndex = indexMap[i];
				if (textMarkType[blockIndex] == MarkType::Number)
				{
					textMarkType[blockIndex] = MarkType::Unassigned;
				}
			}
		}
	}

	void checkTime()
	{
		const auto reg = UR"(\d\d:\d\d)"_re;
		const auto match = reg.search(allText);

		if (!match.isEmpty())
		{
			const StringView textView = allText;
			const auto matchedView = match[0].value();
			const auto beginIndex = &*matchedView.begin() - &*textView.begin();
			for (auto i : step(matchedView.size()))
			{
				const auto blockIndex = indexMap[beginIndex + i];
				textMarkType[blockIndex] = MarkType::Time;
			}
		}
	}

	void checkPrice()
	{
		const auto reg = UR"([*¥][0-9]+)"_re;
		const auto matchList = reg.findAll(allText);

		for (const auto& match : matchList)
		{
			const StringView textView = allText;
			const auto matchedView = match[0].value();
			const auto beginIndex = &*matchedView.begin() - &*textView.begin();
			for (auto i : step(matchedView.size()))
			{
				const auto blockIndex = indexMap[beginIndex + i];
				if (1 <= i) // 改行を挟んだら数字が続いてても打ち切る
				{
					const auto prevBlockIndex = indexMap[beginIndex + i - 1];
					const auto& prevPos = textGroup[prevBlockIndex.x][prevBlockIndex.y].BoundingPoly[0];
					const auto& currentPos = textGroup[blockIndex.x][blockIndex.y].BoundingPoly[0];
					if (currentPos.x < prevPos.x)
					{
						break;
					}
				}
				textMarkType[blockIndex] = MarkType::Price;
			}
		}
	}

	void checkNumber()
	{
		const auto reg = UR"(-?[1-9][0-9]*)"_re;
		const auto matchList = reg.findAll(allText);

		const int thresholdX = image.width() * 0.6;

		for (const auto& [groupIndex, group] : Indexed(textGroup))
		{
			for (const auto& [textIndex, text] : Indexed(group))
			{
				if (thresholdX < text.BoundingPoly[0].x - topLeft.x)
				{
					if (reg.fullMatch(text.Description))
					{
						textMarkType[Point(groupIndex, textIndex)] = MarkType::Number;
					}
				}
			}
		}
	}

	void checkIgnore()
	{
		// ["計","外税","軽減","税率","対象"]の文字以下の座標は無視する
		const auto reg = UR"(計|外税|軽減|税率|対象)"_re;
		const auto match = reg.search(allText);

		if (!match.isEmpty())
		{
			const StringView textView = allText;
			const auto matchedView = match[0].value();
			const auto beginIndex = &*matchedView.begin() - &*textView.begin();
			for (size_t i = beginIndex; i < allText.size(); ++i)
			{
				const auto blockIndex = indexMap[i];
				textMarkType[blockIndex] = MarkType::Ignore;
			}
		}
	}

	void checkItemName()
	{
		const auto reg = UR"([^*¥◆■]+)"_re;

		for (const auto& [groupIndex, group] : Indexed(textGroup))
		{
			if (2 <= group.size())
			{
				bool isItemName = false;
				for (size_t i = 0; i < group.size(); ++i)
				{
					const size_t textIndex = group.size() - 1 - i;
					const auto& currentText = group[textIndex];
					const auto currentMark = textMarkType[Point(groupIndex, textIndex)];
					if (isItemName)
					{
						if (currentMark == MarkType::Date || currentMark == MarkType::ShopName)
						{
							break;
						}

						if (reg.fullMatch(currentText.Description))
						{
							textMarkType[Point(groupIndex, textIndex)] = MarkType::Goods;
						}
					}
					else
					{
						if (currentMark == MarkType::Price || currentMark == MarkType::Number)
						{
							isItemName = true;
						}

						// 値引きの場合は直前は品名ではない
						if (currentText.Description.starts_with(U'-'))
						{
							break;
						}
					}
				}
			}
		}
	}
};

#include <iostream>
#include <vector>
#include <numeric>

class UnionFind
{
public:

	UnionFind() = default;

	/// @brief Union-Find 木を構築します。
	/// @param n 要素数
	explicit UnionFind(size_t n)
		: m_parents(n)
	{
		std::iota(m_parents.begin(), m_parents.end(), 0);
	}

	/// @brief 頂点 i の root のインデックスを返します。
	/// @param i 調べる頂点のインデックス
	/// @return 頂点 i の root のインデックス
	int find(int i)
	{
		if (m_parents[i] == i)
		{
			return i;
		}

		// 経路圧縮
		return (m_parents[i] = find(m_parents[i]));
	}

	/// @brief a のグループと b のグループを統合します。
	/// @param a 一方のインデックス
	/// @param b 他方のインデックス
	void merge(int a, int b)
	{
		a = find(a);
		b = find(b);

		if (a != b)
		{
			m_parents[b] = a;
		}
	}

	/// @brief a と b が同じグループに属すかを返します。
	/// @param a 一方のインデックス
	/// @param b 他方のインデックス
	/// @return a と b が同じグループに属す場合 true, それ以外の場合は false
	bool connected(int a, int b)
	{
		return (find(a) == find(b));
	}

private:

	// m_parents[i] は i の 親,
	// root の場合は自身が親
	std::vector<int> m_parents;
};

struct Group
{
	Array<size_t> largeGroup;
	OrderedTable<size_t, Array<size_t>> smallGroup;
};

#include <fstream>
void DumpResult(std::istream& is)
{
	std::ofstream ofs("dump.txt");

	std::string line;
	while (std::getline(is, line))
	{
		ofs << line;
	}
}

String WrapByWidth(const Font& font, const String& str, double width)
{
	String drawStr;
	for (auto c : str)
	{
		if (width < font(drawStr + c).region().w)
		{
			drawStr += U'\n';
		}
		drawStr += c;
	}
	return drawStr;
}

enum Label
{
	ItemName,
	ItemPrice,
	ShopName,
	BuyDate,
	RegisterDate,
	ReceiptID,
	Size,
};

enum class AlignPos
{
	Left,
	Center,
	Right,
};

enum class WidthType
{
	Relative,
	Absolute,
};

struct TextEditor
{
	TextEditState state;
	Optional<MarkType> editType;
	size_t editIndex;
	size_t editIndex2;
};

struct ItemEditData
{
	String name;
	int32 price = 0;
	Array<int32> discount;
	Rect nameTexRegion = Rect::Empty();
	Rect priceTexRegion = Rect::Empty();
	Array<Rect> discountTexRegion;

	bool isNameEmpty() const
	{
		return nameTexRegion.isEmpty();
	}

	bool isPriceEmpty() const
	{
		return priceTexRegion.isEmpty();
	}
};

template <class T>
struct Greater
{
	inline bool operator()(const T& a, const T& b) const
	{
		return std::greater<T>()(a, b);
	}
};

class TileButton
{
public:

	struct Palette
	{
		ColorF tileColor1;
		ColorF tileColor2;
		ColorF borderColor1;
		ColorF borderColor2;
		//ColorF iconColor;
	};

	TileButton() = default;

	TileButton(const Icon& icon, int32 iconSize, const Palette& palette, ColorF iconColor, bool initialValue = false)
		: TileButton{ Texture{ icon, iconSize }, iconSize, palette, iconColor, initialValue } {}

	// Texture からアイコンを作成
	TileButton(const TextureRegion& textureRegion, int32 iconSize, const Palette& palette, ColorF iconColor, bool initialValue = false)
		: m_icon{ textureRegion }
		, m_iconSize{ iconSize }
		, m_palette{ palette }
		, m_iconColor{ iconColor }
		, m_pushed{ initialValue }
	{}

	Optional<bool> update(const RectF& rect)
	{
		const bool mouseOver = rect.mouseOver();

		if (mouseOver)
		{
			Cursor::RequestStyle(CursorStyle::Hand);
		}

		bool updated = false;

		if (MouseL.down())
		{
			if (mouseOver)
			{
				updated = true;
				m_pushed = !m_pushed;
			}
		}

		m_transitionPressed.update(m_pushed);
		if (m_pushed && m_lateRelease)
		{
			if (m_transitionPressed.isOne())
			{
				m_pushed = false;
				m_lateRelease = false;
			}
		}

		draw(rect);

		if (updated)
		{
			return m_pushed;
		}
		else
		{
			return none;
		}
	}

	void release()
	{
		m_pushed = false;
	}

	void lateRelease()
	{
		m_lateRelease = true;
	}

	void draw(const RectF& rect) const
	{
		const double t = m_transitionPressed.value();

		// タイル
		if (false)
		{
			rect.draw(m_palette.tileColor1.lerp(m_palette.tileColor2, t));

			//if (m_pushed)
			{
				rect.stretched(Math::Lerp(-InnerBorderMargin, 0, t))
					.drawFrame(0.0, t * 3.0, m_palette.borderColor1.lerp(m_iconColor, t));
			}
		}
		{
			rect.draw(m_palette.tileColor2);

			//if (m_pushed)
			{
				rect.stretched(Math::Lerp(-InnerBorderMargin, 0, t))
					.drawFrame(0, t * 3.0, m_palette.borderColor1.lerp(m_iconColor, t));
			}
		}

		// アイコン
		if (false)
		{
			m_icon.drawAt(rect.center(), m_palette.tileColor2.lerp(m_iconColor, t));
		}

		{
			m_icon.drawAt(rect.center(), m_palette.tileColor1.lerp(m_iconColor, t));
		}
	}

private:

	static constexpr double InnerBorderMargin = 3.0;

	TextureRegion m_icon;
	int32 m_iconSize = 0;
	Transition m_transitionPressed{ 0.09s, 0.12s };
	Palette m_palette;
	bool m_pushed = false;
	bool m_lateRelease = false;
	ColorF m_iconColor;
};

struct EditedData
{
	String shopName;
	Date date;
	int32 hours = 0;
	int32 minutes = 0;
	TextEditor textEdit;
	Array<ItemEditData> itemData;
	OrderedTable<String, SimpleTable, Greater<String>> tableDataList; // 登録日時→登録データ
	SimpleTable temporaryData;

	void makeTemporary()
	{
		temporaryData = makeDefaultTable();

		const auto idStr = id();
		const auto dateStr = buyDateFormat();
		const auto now = DateTime::Now();
		const auto nowStr = now.format(U"yyyy年MM月dd日HH時mm分ss秒");

		for (const auto& item : itemData)
		{
			int32 price = item.price;
			for (const auto& v : item.discount)
			{
				price += v;
			}

			const String priceStr = Format(item.price);

			temporaryData.push_back_row({ item.name, priceStr, shopName, dateStr }, { -1,1,0,0 });
		}
	}

	void reloadCSV()
	{
		// 同一日に購入したデータのリスト
		auto filteredRows = searchData();
		tableDataList.clear();

		// 登録日時でグループ化して削除できるようにする
		for (const auto& row : filteredRows)
		{
			const auto& key = row[Label::RegisterDate];
			if (!tableDataList.contains(key))
			{
				tableDataList.emplace(key, makeDefaultTable());
			}

			tableDataList[key].push_back_row({ row[0], row[1], row[2], row[3] }, { -1,1,0,0 });
		}

		makeTemporary();
	}

	SimpleTable makeDefaultTable()
	{
		SimpleTable newData{ { 50,50,50,50 }, {
			.variableWidth = true,
			.font = Font(14, Typeface::Regular),
			.fontSize = 14,
			.columnHeaderFont = Font(14, Typeface::Bold),
			.columnHeaderFontSize = 14,
			} };

		newData.push_back_row({ U"商品" ,U"価格", U"店名", U"購入日" }, { 0,0,0,0 });

		return newData;
	}

	void conirmTextEdit()
	{
		switch (textEdit.editType.value())
		{
		case MarkType::ShopName:
			shopName = textEdit.state.text;
			break;
		case MarkType::Date:
			if (auto opt = ParseIntOpt<int32>(textEdit.state.text, 10))
			{
				if (textEdit.editIndex == 0)
				{
					date.year = opt.value();
				}
				else if (textEdit.editIndex == 1)
				{
					date.month = opt.value();
				}
				else if (textEdit.editIndex == 2)
				{
					date.day = opt.value();
				}
				reloadCSV();
			}
			break;
		case MarkType::Time:
			if (auto opt = ParseIntOpt<int32>(textEdit.state.text, 10))
			{
				if (textEdit.editIndex == 0)
				{
					hours = opt.value();
				}
				else if (textEdit.editIndex == 1)
				{
					minutes = opt.value();
				}
				reloadCSV();
			}
			break;
		case MarkType::Goods:
			itemData[textEdit.editIndex].name = textEdit.state.text;
			makeTemporary();
			break;
		case MarkType::Price: [[fallthrough]];
		case MarkType::Number:
			if (auto opt = ParseIntOpt<int32>(textEdit.state.text, 10))
			{
				if (textEdit.editIndex2 == 0)
				{
					itemData[textEdit.editIndex].price = opt.value();
				}
				else
				{
					itemData[textEdit.editIndex].discount[textEdit.editIndex2 - 1] = opt.value();
				}
			}
			makeTemporary();
			break;
		default: break;
		}
	}

	RectF drawEditableText(const String& str, const Font& font, const Vec2& pos, Color color, MarkType markType, size_t editIndex, size_t editIndex2, double width, bool isFocus)
	{
		const auto text = font(WrapByWidth(font, str, width));
		auto region = text.region(pos);

		if (isFocus)
		{
			if (textEdit.editType && textEdit.editType.value() == markType && textEdit.editIndex == editIndex && textEdit.editIndex2 == editIndex2)
			{
				SimpleGUI::TextBox(textEdit.state, pos - Vec2(4, 4), width);
			}
			else
			{
				text.draw(pos).drawFrame(1.0, color);

				if (region.mouseOver())
				{
					region.drawFrame(1.0, Palette::White);
				}

				if (region.leftClicked())
				{
					if (textEdit.editType)
					{
						conirmTextEdit();
					}
					textEdit.state.text = str;
					textEdit.state.active = true;
					textEdit.editType = markType;
					textEdit.editIndex = editIndex;
					textEdit.editIndex2 = editIndex2;
				}
			}
		}
		else
		{
			text.draw(pos).drawFrame(1.0, color);
		}

		return region;
	}

	RectF draw(const Vec2& pos0, const Font& font, const Texture& texture, const Point& textureTopLeft, double drawScale, bool isFocus)
	{
		if (itemData.empty())
		{
			return RectF();
		}

		double sumOfTexHeight = 0.0;
		int32 numOfTexHeight = 0;
		for (const auto& [itemIndex, item] : Indexed(itemData))
		{
			if (!item.isNameEmpty())
			{
				sumOfTexHeight += item.nameTexRegion.h;
				++numOfTexHeight;
			}
			if (!item.isPriceEmpty())
			{
				sumOfTexHeight += item.priceTexRegion.h;
				++numOfTexHeight;
			}
		}

		const double averageTexHeight = sumOfTexHeight / numOfTexHeight;
		drawScale = 0.8 * font.height() / averageTexHeight;

		const int32 maxNameWidth = 300;
		const int32 maxPriceWidth = 80;

		const int xMargin = 10;
		const int yMargin = 12;
		const int yInnerMargin = 8;

		const int xOuterMargin = 40;
		const int yOuterMargin = 50;

		Array<double> nameWidth;
		Array<double> priceWidth;
		Array<double> namePriceHeight;

		Array<Array<double>> discountWidth;

		size_t maxDiscountIndex = 0;
		int32 sumOfPrice = 0;
		for (const auto& [itemIndex, item] : Indexed(itemData))
		{
			double maxHeight = 0;

			const auto nameStrRegion = font(WrapByWidth(font, item.name, maxNameWidth - xMargin)).region();
			const auto nameTexRegion = texture(item.nameTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).region();
			nameWidth.push_back(Max(nameStrRegion.w, nameTexRegion.w) + xMargin);//最初の左端だけ空けないのでマージンは1つ分
			maxHeight = Max(maxHeight, nameStrRegion.h + nameTexRegion.h);

			const auto priceStrRegion = font(WrapByWidth(font, Format(item.price), maxPriceWidth - xMargin * 2)).region();
			const auto priceTexRegion = texture(item.priceTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).region();
			priceWidth.push_back(Max(priceStrRegion.w, priceTexRegion.w) + xMargin * 2);
			maxHeight = Max(maxHeight, priceStrRegion.h + priceTexRegion.h);

			sumOfPrice += item.price;

			{
				Array<double> currentWidth;

				for (const auto& [discountIndex, discount] : Indexed(item.discount))
				{
					const auto discountStrRegion = font(WrapByWidth(font, Format(discount), maxPriceWidth - xMargin * 2)).region();
					const auto discountTexRegion = texture(item.discountTexRegion[discountIndex].movedBy(-textureTopLeft)).scaled(drawScale).region();
					currentWidth.push_back(Max(discountStrRegion.w, discountTexRegion.w) + xMargin * 2);
					maxHeight = Max(maxHeight, discountStrRegion.h + discountTexRegion.h);

					sumOfPrice += discount;
					maxDiscountIndex = Max(maxDiscountIndex, discountIndex);
				}

				discountWidth.push_back(currentWidth);
			}

			namePriceHeight.push_back(maxHeight + yInnerMargin + yMargin * 2);
		}

		const bool showSum = true;
		// 合計額
		if (showSum)
		{
			const auto priceStrRegion = font(WrapByWidth(font, Format(sumOfPrice), maxPriceWidth - xMargin * 2)).region();
			priceWidth.push_back(priceStrRegion.w + xMargin * 2);
			namePriceHeight.push_back(priceStrRegion.h + yMargin * 2);
		}

		const double calcMaxNameWidth = *std::max_element(nameWidth.begin(), nameWidth.end());
		const double calcMaxPriceWidth = *std::max_element(priceWidth.begin(), priceWidth.end());

		// 各列の最大幅を計算
		Array<double> calcMaxDiscountWidth(maxDiscountIndex + 1, 0.0);
		for (const auto& [itemIndex, itemDiscounts] : Indexed(discountWidth))
		{
			for (const auto& [discountIndex, width] : Indexed(itemDiscounts))
			{
				calcMaxDiscountWidth[discountIndex] = Max(calcMaxDiscountWidth[discountIndex], width);
			}
		}

		Array<double> sumHeight(1, 0);
		std::partial_sum(namePriceHeight.begin(), namePriceHeight.end(), std::back_inserter(sumHeight));

		const double sumOfWidth = calcMaxNameWidth + calcMaxPriceWidth + calcMaxDiscountWidth.sum();

		int32 maxWidth = sumOfWidth;

		Vec2 pos_ = pos0 + Vec2(xOuterMargin, yOuterMargin);
		{
			auto rect = drawEditableText(shopName, font, pos_, Palette::Orange.withAlpha(255), MarkType::ShopName, 0, 0, maxWidth, isFocus);
			pos_ = rect.bl() + Vec2(0, yMargin * 2);
		}
		{
			pos_ += Vec2(0, yMargin);
			auto rect = drawEditableText(Format(date.year), font, pos_, Palette::Greenyellow.withAlpha(255), MarkType::Date, 0, 0, 60, isFocus);
			auto rect1 = font(U"年").draw(rect.tr() + Vec2(10, 0), Palette::White);
			rect1 = drawEditableText(Format(date.month), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Date, 1, 0, 30, isFocus);
			rect1 = font(U"月").draw(rect1.tr() + Vec2(10, 0), Palette::White);
			rect1 = drawEditableText(Format(date.day), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Date, 2, 0, 30, isFocus);
			rect1 = font(U"日").draw(rect1.tr() + Vec2(10, 0), Palette::White);

			rect1 = drawEditableText(Format(hours), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Time, 0, 0, 30, isFocus);
			rect1 = font(U"時").draw(rect1.tr() + Vec2(10, 0), Palette::White);
			rect1 = drawEditableText(Format(minutes), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Time, 1, 0, 30, isFocus);
			rect1 = font(U"分").draw(rect1.tr() + Vec2(10, 0), Palette::White);
			pos_ = rect.bl() + Vec2(0, yMargin * 2);
		}

		Array<RectF> nameRects;
		Array<RectF> priceRects;
		Array<Array<RectF>> discountRects;

		for (const auto& [itemIndex, item] : Indexed(itemData))
		{
			const auto nameRect = RectF(pos_.x, pos_.y + sumHeight[itemIndex], calcMaxNameWidth, namePriceHeight[itemIndex]).stretched(-yMargin, -xMargin, -yMargin, 0);
			const auto priceRect = RectF(pos_.x + calcMaxNameWidth, pos_.y + sumHeight[itemIndex], calcMaxPriceWidth, namePriceHeight[itemIndex]).stretched(-xMargin, -yMargin);

			Array<RectF> currentRects;
			for (const auto& [discountIndex, discount] : Indexed(item.discount))
			{
				const auto discountRect = RectF(
					pos_.x + calcMaxNameWidth + calcMaxPriceWidth,
					pos_.y + sumHeight[itemIndex],
					calcMaxDiscountWidth[discountIndex],
					namePriceHeight[itemIndex]).stretched(-xMargin, -yMargin);
				currentRects.push_back(discountRect);
			}
			discountRects.push_back(currentRects);

			nameRects.push_back(nameRect);
			priceRects.push_back(priceRect);
		}

		if (showSum)
		{
			const auto priceRect = RectF(pos_.x + calcMaxNameWidth, pos_.y + sumHeight[itemData.size()] + yMargin * 2, calcMaxPriceWidth, namePriceHeight[itemData.size()]).stretched(-xMargin, -yMargin);
			priceRects.push_back(priceRect);
		}

		for (const auto& [itemIndex, item] : Indexed(itemData))
		{
			const auto& namePos = nameRects[itemIndex].pos;
			auto nameRect = drawEditableText(item.name, font, namePos, Palette::Lime.withAlpha(255), MarkType::Goods, itemIndex, 0, calcMaxNameWidth - xMargin, isFocus);
			auto nameTexRect = texture(item.nameTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).draw(nameRect.bl() + Vec2(0, yInnerMargin));

			const auto pricePos = priceRects[itemIndex].pos;
			auto priceRect = drawEditableText(Format(item.price), font, pricePos, Palette::Cyan.withAlpha(255), MarkType::Price, itemIndex, 0, calcMaxPriceWidth - xMargin * 2, isFocus);
			auto priceTexRect = texture(item.priceTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).draw(priceRect.bl() + Vec2(0, yInnerMargin));

			for (const auto& [discountIndex, discount] : Indexed(item.discount))
			{
				const auto& rect = discountRects[itemIndex][discountIndex];
				auto discountRect = drawEditableText(Format(discount), font, rect.pos, Palette::Cyan.withAlpha(255), MarkType::Price, itemIndex, 1 + discountIndex, calcMaxDiscountWidth[discountIndex] - xMargin * 2, isFocus);
				auto discountTexRect = texture(item.discountTexRegion[discountIndex].movedBy(-textureTopLeft)).scaled(drawScale).draw(discountRect.bl() + Vec2(0, yInnerMargin));
			}
		}

		if (showSum)
		{
			Line(pos0.x + xOuterMargin, nameRects.back().bottomY() + yMargin * 2, pos0.x + xOuterMargin + sumOfWidth, nameRects.back().bottomY() + yMargin * 2).draw();
			const auto sumRect = font(sumOfPrice).draw(priceRects.back().pos);
		}

		return RectF(pos0, sumOfWidth + xOuterMargin * 2, priceRects.back().bottomY() - pos0.y + yOuterMargin);
	}

	String id() const
	{
		return U"ID{:0>4}{:0>2}{:0>2}{:0>2}{:0>2}"_fmt(date.year, date.month, date.day, hours, minutes);
	}

	String buyDateFormat() const
	{
		return date.format(U"yyyy年MM月dd日");
	}

	String csvPath() const
	{
		return date.format(U"yyyy年MM月") + U".csv";
	}

	CSV openCSV() const
	{
		return CSV(csvPath());
	}

	// 品名, 値段, 店名, 購入日, 登録日時, レシートID
	void writeData() const
	{
		auto csv = openCSV();

		const auto idStr = id();
		const auto dateStr = buyDateFormat();
		const auto now = DateTime::Now();
		const auto nowStr = now.format();

		for (const auto& item : itemData)
		{
			int32 price = item.price;
			for (const auto& v : item.discount)
			{
				price += v;
			}

			csv.writeRow(item.name, item.price, shopName, dateStr, nowStr, idStr);
		}

		csv.save(csvPath());
	}

	void deleteByRegisterDate(const String& registerDate) const
	{
		auto readCsv = openCSV();
		CSV writeCSV;

		for (auto i : step(readCsv.rows()))
		{
			const auto row = readCsv.getRow(i);
			if (Label::Size <= row.size())
			{
				if (row[Label::RegisterDate] != registerDate)
				{
					for (const auto& cell : row)
					{
						writeCSV.write(cell);
					}
					writeCSV.newLine();
				}
				else
				{
					for (const auto& cell : row)
					{
						//Console << U"削除：" << cell;
					}
				}
			}
		}

		writeCSV.save(csvPath());
	}

	Array<Array<String>> searchData() const
	{
		const auto csv = openCSV();
		const auto searchID = buyDateFormat();

		Array<Array<String>> matchedRows;

		for (auto i : step(csv.rows()))
		{
			const auto row = csv.getRow(i);
			if (Label::Size <= row.size())
			{
				if (row[Label::BuyDate] == searchID)
				{
					matchedRows.push_back(row);
				}
			}
		}

		return matchedRows;
	}
};

constexpr TileButton::Palette Palette1{
	.tileColor1 = Palette::Gainsboro,
	.tileColor2 = Color{ 33, 33, 33 },
	.borderColor1 = Palette::Black,
	.borderColor2 = Palette::White,
};

class ReceiptEditor
{
public:

	void calc(const FilePath& path, std::istream& is)
	{
		const auto result = ReadResult(is);
		UnionFind unionFind;
		unionFind = UnionFind(result.size());
		for (size_t i = 0; i < result.size(); ++i)
		{
			for (size_t k = i + 1; k < result.size(); ++k)
			{
				if (result[i].BoundingBox.intersects(result[k].BoundingBox))
				{
					unionFind.merge(i, k);
				}
			}
		}

		HashTable<size_t, Array<Vec2>> groupPolygons;
		HashTable<size_t, Group> groupElements;
		for (size_t i = 0; i < result.size(); ++i)
		{
			const auto groupIndex = unionFind.find(i);
			auto& groupPoly = groupPolygons[groupIndex];
			groupPoly.append(result[i].BoundingPoly);
			auto& group = groupElements[groupIndex];
			group.largeGroup.push_back(i);
		}

		const Image image(path);

		receiptData.clear();

		int i = 0;
		for (auto& [index, val] : groupElements)
		{
			receiptData.emplace_back();
			calculateData(i, image, result, groupPolygons[index], groupElements[index]);
			convertEditData(i);
			++i;
		}

		resetFocus();
	}

	void recalculate(const FilePath& path, std::istream& is, int index)
	{
		auto result = ReadResult(is);
		for (size_t i = 0; i < result.size(); ++i)
		{
			result[i].Description = result[i].Description.replaced(U"\r", U"");
		}

		Array<Vec2> polygons;
		Group groupData;
		for (size_t i = 0; i < result.size(); ++i)
		{
			polygons.append(result[i].BoundingPoly);
			groupData.largeGroup.push_back(i);
		}

		const Image image(path);

		calculateData(index, image, result, polygons, groupData);

		resetFocus();
	}

	void update()
	{
		if (receiptData.empty())
		{
			return;
		}

		if (editedData[focusIndex].textEdit.editType)
		{
			if (!editedData[focusIndex].textEdit.state.active)
			{
				editedData[focusIndex].conirmTextEdit();
				editedData[focusIndex].textEdit.editType = none;
			}

			return;
		}

		if (penType)
		{
			auto t = camera.createTransformer();
			if (dragStartPos)
			{
				if (!MouseL.pressed())
				{
					const auto startPos = dragStartPos.value();
					const auto endPos = Cursor::PosF();
					selectRange = RectF(startPos, endPos - startPos);

					dragStartPos = none;
				}

				// shiftを離したらキャンセル
				if (!KeyShift.pressed())
				{
					dragStartPos = none;
				}
			}
			else if (KeyShift.pressed() && MouseL.down())
			{
				dragStartPos = Cursor::PosF();
			}
		}

		if (KeyUp.down())
		{
			zoomIn();
		}
		if (KeyDown.down())
		{
			zoomOut();
		}
		if (KeyC.down())
		{
			camera.setTargetScale(defaultCameraScale);
			resetFocus();
		}

		const int prevIndex = focusIndex;

		if (KeyRight.down() || KeySpace.down())
		{
			focusIndex += 1;
		}
		else if (KeyLeft.down())
		{
			focusIndex -= 1;
		}

		focusIndex += receiptData.size();
		focusIndex = focusIndex % receiptData.size();

		if (prevIndex != focusIndex)
		{
			camera.setTargetScale(defaultCameraScale);
			camera.setScale(defaultCameraScale);
			rightAreaVerticalOffset = 0;
			resetFocus();
		}

		// 編集データの削除
		if (KeyShift.pressed() && KeyEnter.down() && editedData.contains(focusIndex))
		{
			editedData.erase(focusIndex);
		}
		// 編集データの作成
		else if (KeyEnter.down() && !editedData.contains(focusIndex))
		{
			convertEditData(focusIndex);
		}
	}

	void zoomIn()
	{
		camera.setTargetScale(camera.getTargetScale() * 1.3);
	}

	void zoomOut()
	{
		camera.setTargetScale(camera.getTargetScale() * 0.7);
	}

	void draw()
	{
		if (receiptData.empty())
		{
			return;
		}

		if (!editedData[focusIndex].textEdit.editType)
		{
			camera.update();
		}

		const auto markerRect = updateUI2();
		const double margin1 = 20;
		const Vec2 scopePos = markerRect.tr() + Vec2(margin1, 0);

		titleFont(focusIndex + 1, U" / ", receiptData.size(), U" 件目").draw(Arg::bottomLeft = scopePos - Vec2(0, 10));

		{
			for (auto [receiptIndex, data] : IndexedRef(receiptData))
			{
				if (focusIndex != receiptIndex)
				{
					continue;
				}

				{
					auto scope = getScreenScope(scopePos);

					const auto receiptAreaMouseOver = scope.mouseOver();

					auto t1 = Transformer2D(Mat3x2::Translate((scope.center() - Scene::CenterF()) / camera.getScale()), TransformCursor::Yes);
					scope.pos = Vec2::Zero();

					const auto tex = data.texture.scaled(drawScale);

					RectF textRect = RectF(Arg::topLeft = scope.tr() + Vec2(marginX, 0), 500 * drawScale, 50 * drawScale);

					auto t2 = camera.createTransformer();

					Graphics2D::SetScissorRect(getScreenScope(scopePos).asRect());
					RasterizerState rs = RasterizerState::Default2D;
					rs.scissorEnable = true;
					const ScopedRenderStates2D rasterizer{ rs };

					tex.drawAt(Vec2::Zero());

					// 左のデバッグ描画
					if (showBoundingPoly)
					{
						String shopName;
						String dateStr;
						String timeStr;
						Array<String> itemNames;
						Array<String> itemPrices;
						for (const auto& [groupIndex, group] : Indexed(data.textGroup))
						{
							String goodsStr;
							String priceStr;
							double minX = DBL_MAX;
							double minY = DBL_MAX;
							double maxX = -DBL_MAX;
							double maxY = -DBL_MAX;

							for (const auto& [textIndex, text] : Indexed(group))
							{
								const auto type = data.textMarkType.at(Point(groupIndex, textIndex));

								switch (type)
								{
								case MarkType::ShopName:
									shopName += text.Description;
									break;
								case MarkType::Date:
									dateStr += text.Description;
									break;
								case MarkType::Time:
									timeStr += text.Description;
									break;
								case MarkType::Goods:
								{
									goodsStr += text.Description;
									const auto minMaxX = text.minMaxX();
									const auto minMaxY = text.minMaxY();
								}
								break;
								case MarkType::Price: [[fallthrough]];
								case MarkType::Number:
								{
									priceStr += text.Description;
									const auto minMaxX = text.minMaxX();
									const auto minMaxY = text.minMaxY();
								}
								break;
								default:
									break;
								}
							}

							RectF rect(minX, minY, maxX - minX, maxY - minY);

							const auto originalPos = (Vec2(minX, minY) + Vec2(maxX, maxY)) * 0.5;
							const auto localPos = originalPos - data.topLeft;
							const auto textDrawPos = localPos * drawScale;
							if (!goodsStr.empty())
							{
								itemNames.push_back(goodsStr);
							}
							if (!priceStr.empty())
							{
								itemPrices.push_back(priceStr);
							}

							for (const auto& [textIndex, text] : Indexed(group))
							{
								const auto textPoly = LineString(text.BoundingPoly).scaledAt(data.topLeft, drawScale).movedBy(-data.topLeft - data.texture.size());
								const auto type = data.textMarkType.at(Point(groupIndex, textIndex));

								if (type == MarkType::Ignore || type == MarkType::Unassigned)
								{
									textPoly.drawClosed(2.0, MarkColor[static_cast<int32>(type)].withAlpha(64));
								}
								else
								{
									textPoly.drawClosed(2.0, MarkColor[static_cast<int32>(type)]);
								}

								const Polygon textPolygon(textPoly);
								if (penType && receiptAreaMouseOver)
								{
									if (!dragStartPos && !selectRange)
									{
										if (textPolygon.mouseOver())
										{
											textPolygon.draw(Alpha(64));
										}
										if (textPolygon.leftPressed())
										{
											if (data.textMarkType[Point(groupIndex, textIndex)] != penType.value())
											{
												data.textMarkType[Point(groupIndex, textIndex)] = penType.value();
												data.updatedMarkIndices.emplace(Point(groupIndex, textIndex));
											}
										}
									}
									else if (selectRange)
									{
										if (selectRange.value().contains(textPolygon))
										{
											if (data.textMarkType[Point(groupIndex, textIndex)] != penType.value())
											{
												data.textMarkType[Point(groupIndex, textIndex)] = penType.value();
												data.updatedMarkIndices.emplace(Point(groupIndex, textIndex));
											}
										}
									}
									else if (dragStartPos)
									{
										const auto startPos = dragStartPos.value();
										const auto endPos = Cursor::PosF();
										const RectF currentRange(startPos, endPos - startPos);

										if (currentRange.contains(textPolygon))
										{
											textPolygon.draw(Alpha(64));
										}
									}
								}
							}

							if (penType && dragStartPos)
							{
								const auto startPos = dragStartPos.value();
								const auto endPos = Cursor::PosF();
								const RectF currentRange(startPos, endPos - startPos);

								currentRange.drawFrame(2.0, MarkColor[static_cast<int32>(penType.value())]);
							}
						}

						selectRange = none;

						if (!data.updatedMarkIndices.empty())
						{
							convertEditData(receiptIndex);
						}
					}

					camera.draw();
				}

				auto scope = getScreenScope(scopePos);
				RectF textRect = RectF(Arg::topLeft = scope.tr() + Vec2(marginX, 0), 500 * drawScale, 50 * drawScale);

				if (editedData.contains(receiptIndex))
				{
					auto& editData = editedData.at(receiptIndex);

					// 中央のレシート読み取り結果
					const auto editRect = editData.draw(textRect.pos, mediumFont, data.texture, data.topLeft.asPoint(), camera.getTargetScale(), receiptIndex == focusIndex);
					RectF(editRect.pos, editRect.w, Scene::Height() - topMargin * 2).drawFrame();

					// 右の表
					if (!editRect.isEmpty())
					{
						const Vec2 textRect2Pos = editRect.tr() + Vec2(marginX, 0);
						const RectF textRect2_ = RectF(textRect2Pos, Scene::Width() - leftMargin - textRect2Pos.x, Scene::Height() - topMargin * 2);

						{
							const RectF textRect2 = textRect2_.stretched(-40);

							Graphics2D::SetScissorRect(textRect2.asRect());
							RasterizerState rs = RasterizerState::Default2D;
							rs.scissorEnable = true;
							const ScopedRenderStates2D rasterizer{ rs };

							RectF tableRegion = RectF(textRect2.pos, textRect2.size);

							if (textRect2.mouseOver())
							{
								rightAreaVerticalOffset += Mouse::Wheel() * -50.0;
							}

							rightAreaVerticalOffset = Min(0.0, rightAreaVerticalOffset);

							Vec2 pos = textRect2.pos + Vec2(0, rightAreaVerticalOffset);
							{
								const auto titleFontRegion = largeFont(U" 現在の編集データ ").draw(pos);
								pos = titleFontRegion.bl() + Vec2(0, 10);

								const auto region2 = editData.temporaryData.region(pos);

								if (auto updated = saveButton.update(RectF(Arg::leftCenter = titleFontRegion.rightCenter(), buttonSize)))
								{
									if (updated.value())
									{
										auto& editData = editedData.at(focusIndex);

										editData.writeData();
										editData.reloadCSV();
										saveButton.lateRelease();
									}
								}

								editData.temporaryData.draw(pos);
								pos = region2.bl() + Vec2(0, 30);
							}

							if (editData.tableDataList.empty())
							{
								const auto region2 = largeFont(editData.csvPath(), U" > ", U" 未登録データ").draw(pos);
								pos = region2.bl() + Vec2(0, 10);
							}
							else
							{
								const auto region2 = largeFont(editData.buyDateFormat(), U" 購入分のレシート記録：", editData.tableDataList.size(), U"件").draw(pos);
								pos = region2.bl() + Vec2(0, 10);
							}

							Optional<String> deleteRegisterDate;
							for (const auto& [dataIndex, data] : Indexed(editData.tableDataList))
							{
								const auto& [registerDate, tableData] = data;

								if (2 <= tableData.rows())
								{
									const auto region = largeFont(U"[{}] "_fmt(dataIndex), registerDate, U" に登録 ").draw(pos);
									pos += Vec2(0, region.h) + Vec2(0, 10);

									if (auto updated = deleteButton.update(RectF(Arg::leftCenter = region.rightCenter(), buttonSize)))
									{
										if (updated.value())
										{
											deleteRegisterDate = registerDate;
											deleteButton.lateRelease();
										}
									}
								}

								tableRegion = RectF(textRect2.x, pos.y, textRect2.w, textRect2.h);

								const auto region = tableData.region(tableRegion.pos);
								tableData.draw(tableRegion.pos);
								pos = region.bl() + Vec2(0, 10);

								if (false)
								{
									const double buttonScale = 0.5;
									const double scale_ = 0.7;

									const RectF fixButton(Arg::bottomRight = region.tr(), updateIcon.size() * buttonScale);

									auto i = 1;
									const auto& texture = writeIcons[i];

									//for (const auto [i, texture] : Indexed(writeIcons))
									{
										texture.scaled(buttonScale * scale_).drawAt(fixButton.center());
									}

									//for (const auto [i, texture] : Indexed(writeIcons))
									{
										const auto buttonRect = fixButton;
										if (buttonRect.mouseOver())
										{
											buttonRect.draw(Palette::Lightyellow.withAlpha(60));
										}
										if (buttonRect.leftClicked())
										{
											switch (i)
											{
											case 0:
											{
												auto& editData = editedData.at(focusIndex);

												editData.writeData();
												editData.reloadCSV();
												break;
											}
											case 1:
											{
												deleteRegisterDate = registerDate;
												break;
											}
											}
										}
									}
								}
							}

							if (deleteRegisterDate)
							{
								auto& editData = editedData.at(focusIndex);

								editData.deleteByRegisterDate(deleteRegisterDate.value());
								editData.reloadCSV();
							}
						}

						textRect2_.drawFrame();
					}
				}
			}
		}

		getScreenScope(scopePos).drawFrame();
	}

	RectF updateUI2()
	{
		const Vec2 pos(leftMargin, topMargin);

		const double margin1 = 20;
		const double margin2 = 10;

		auto rect1 = RectF(pos, buttonSize);
		auto rect2 = rect1.movedBy(rect1.w + margin2, 0);
		auto rect3 = rect1.movedBy(0, rect1.h + margin2);
		auto rect4 = rect2.movedBy(0, rect1.h + margin2);
		auto rect5 = rect3.movedBy(0, rect1.h + margin2);
		auto rect6 = rect4.movedBy(0, rect1.h + margin2);
		Optional<size_t> selected;

		Array<RectF> rects = { rect1,rect2,rect3,rect4,rect5,rect6 };
		for (const auto& [i, rect] : Indexed(rects))
		{
			if (auto changed = markButtons[i].update(rect))
			{
				if (changed.value())
				{
					selected = i;
					penType = markTypes[i];
				}
				else
				{
					penType = none;
				}
			}
		}

		if (selected)
		{
			for (auto [i, button] : IndexedRef(markButtons))
			{
				if (selected.value() != i)
				{
					button.release();
				}
			}
		}

		auto retryRect = rect5.movedBy(0, (rect1.h + margin2) * 2);
		if (auto changed = retryButton.update(retryRect))
		{
			if (changed.value())
			{
				const auto rotateAngle = -receiptData[focusIndex].angle();
				const auto saveFilePath = FileSystem::CurrentDirectory() + U"temp.png";
				receiptData[focusIndex].image.rotated(rotateAngle).savePNG(saveFilePath);

				Window::SetTitle(U"計算中…");
				ChildProcess process(VisionExePath, saveFilePath, Pipe::StdIn);
				auto& is = process.istream();

				recalculate(saveFilePath, is, focusIndex);

				Window::SetTitle(U"レシートOCR");

				retryButton.lateRelease();
			}
		}

		auto showDebugRect = rect5.movedBy(0, (rect1.h + margin2) * 4);
		if (auto changed = showBoundingPolyButton.update(showDebugRect))
		{
			showBoundingPoly = changed.value();
		}

		auto zoomOutRect = rect5.movedBy(0, (rect1.h + margin2) * 6);
		auto zoomInRect = rect6.movedBy(0, (rect1.h + margin2) * 6);
		if (auto changed = largerButton.update(zoomInRect))
		{
			zoomIn();
			largerButton.lateRelease();
		}
		if (auto changed = smallerButton.update(zoomOutRect))
		{
			zoomOut();
			smallerButton.lateRelease();
		}

		return RectF(rect1.tl(), rect6.br() - rect1.tl());
	}

	void updateUI()
	{
		const auto frame = Scene::Rect();

		const RectF buttonFrameLeft(0, 0, updateIcon.width() * iconDrawScale, Scene::Height());
		buttonFrameLeft.draw(Color{ 33, 33, 33 });

		const RectF buttonFrameRight(Arg::topRight = Scene::Rect().tr(), updateIcon.width() * iconDrawScale, Scene::Height());
		buttonFrameRight.draw(Color{ 33, 33, 33 });

		const double scale_ = 0.7;

		{
			const RectF updateButton(Arg::bottomLeft = frame.bl(), updateIcon.size() * iconDrawScale);
			const RectF shopButton(Arg::topLeft = frame.tl(), updateIcon.size() * iconDrawScale);
			updateIcon.scaled(iconDrawScale * scale_).drawAt(updateButton.center());

			if (penType)
			{
				for (const auto& [buttonIndex, type] : Indexed(markTypes))
				{
					if (type == penType.value())
					{
						const auto buttonRect = shopButton.movedBy(Vec2(0, shopButton.h) * buttonIndex);
						//buttonRect.draw(Color{ 41, 41, 41 });
						buttonRect.draw(Palette::Gainsboro);
					}
				}
			}

			for (const auto [i, texture] : Indexed(markIcons))
			{
				if (penType && markTypes[i] == penType.value())
				{
					texture.scaled(iconDrawScale * scale_).drawAt(shopButton.center() + Vec2(0, shopButton.h) * i);
				}
				else
				{
					texture.scaled(iconDrawScale * scale_).drawAt(shopButton.center() + Vec2(0, shopButton.h) * i, Color{ 160 });
				}
			}

			{
				if (updateButton.mouseOver())
				{
					updateButton.draw(Palette::Lightyellow.withAlpha(60));
				}
				if (updateButton.leftClicked())
				{
					const auto rotateAngle = -receiptData[focusIndex].angle();
					const auto saveFilePath = FileSystem::CurrentDirectory() + U"temp.png";
					receiptData[focusIndex].image.rotated(rotateAngle).savePNG(saveFilePath);

					Window::SetTitle(U"計算中…");
					ChildProcess process(VisionExePath, saveFilePath, Pipe::StdIn);
					auto& is = process.istream();

					recalculate(saveFilePath, is, focusIndex);

					Window::SetTitle(U"レシートOCR");
				}
			}

			for (const auto [i, texture] : Indexed(markIcons))
			{
				const auto buttonRect = shopButton.movedBy(Vec2(0, shopButton.h) * i);
				if (buttonRect.mouseOver())
				{
					buttonRect.draw(Palette::Lightyellow.withAlpha(60));
				}
				if (buttonRect.leftClicked())
				{
					if (penType == markTypes[i])
					{
						penType = none;
					}
					else
					{
						penType = markTypes[i];
					}
				}
			}
		}

		if (false)
		{
			const RectF fixButton(Arg::topRight = frame.tr(), updateIcon.size() * iconDrawScale);

			for (const auto [i, texture] : Indexed(writeIcons))
			{
				texture.scaled(iconDrawScale * scale_).drawAt(fixButton.center() + Vec2(0, fixButton.h) * i);
			}

			for (const auto [i, texture] : Indexed(writeIcons))
			{
				const auto buttonRect = fixButton.movedBy(Vec2(0, fixButton.h) * i);
				if (buttonRect.mouseOver())
				{
					buttonRect.draw(Palette::Lightyellow.withAlpha(60));
				}
				if (buttonRect.leftClicked())
				{
					switch (i)
					{
					case 0:
					{
						auto& editData = editedData.at(focusIndex);

						editData.writeData();
						editData.reloadCSV();
						break;
					}
					case 1:
					{
						//auto& editData = editedData.at(focusIndex);

						//editData.deleteByRegisterDate();
						//reloadCSV(focusIndex);
						//break;
					}
					}
				}
			}
		}
	}

	static Array<TextAnnotation> ReadResult(std::istream& is)
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

private:

	void calculateData(int index, const Image& image, const Array<TextAnnotation>& result, Array<Vec2>& polygons, Group& groupData)
	{
		{
			const auto& group = groupData.largeGroup;

			UnionFind unionFind2;
			unionFind2 = UnionFind(group.size());
			for (size_t i = 0; i < group.size(); ++i)
			{
				for (size_t k = i + 1; k < group.size(); ++k)
				{
					const auto minMaxA = result[group[i]].minMaxY();
					const auto minMaxB = result[group[k]].minMaxY();

					// 高さの範囲が一定以上の割合で被っているいたら同じ行とみなす
					const double unionMin = std::min(minMaxA.x, minMaxB.x);
					const double unionMax = std::max(minMaxA.y, minMaxB.y);
					const double intersectionMin = std::max(minMaxA.x, minMaxB.x);
					const double intersectionMax = std::min(minMaxA.y, minMaxB.y);
					if (intersectionMax <= intersectionMin)
					{
						continue;
					}

					const double coverage = (intersectionMax - intersectionMin) / (unionMax - unionMin);
					if (0.5 < coverage)
					{
						unionFind2.merge(i, k);
					}
				}
			}

			for (size_t i = 0; i < group.size(); ++i)
			{
				const auto groupIndex = unionFind2.find(i);
				groupData.smallGroup[static_cast<size_t>(groupIndex)].push_back(group[i]);
			}
		}

		{
			ReceiptData data;
			const auto convexHull = Geometry2D::ConvexHull(polygons);

			const auto clippingRect = convexHull.boundingRect().asRect();
			data.boundingPolygon = convexHull.movedBy(-clippingRect.pos);
			data.image = image.clipped(clippingRect);

			//*
			// 領域外を白で塗りつぶす
			for (int y = 0; y < data.image.height(); ++y)
			{
				for (int x = 0; x < data.image.width(); ++x)
				{
					const auto globalPos = clippingRect.pos + Vec2(x, y);
					if (!convexHull.contains(globalPos))
					{
						data.image[y][x] = Color{ 59, 59, 59 };
					}
				}
			}
			//*/

			data.texture = Texture(data.image);

			data.topLeft = clippingRect.pos;

			for (const auto& group : groupData.smallGroup)
			{
				data.textGroup.emplace_back();
				auto& currentTextGroup = data.textGroup.back();

				for (const auto elemIndex : group.second)
				{
					// elemIndexは昇順になっているはず
					currentTextGroup.push_back(result[elemIndex]);
				}
			}

			receiptData[index] = data;
		}

		receiptData[index].init();
	}

	int32 leftMargin = 60;
	int32 topMargin = 100;

	RectF getScreenScope(const Vec2 pos) const
	{
		const auto sceneRect = Scene::Rect();
		const RectF screenRect(pos, (sceneRect.w / 4.0), (sceneRect.h - topMargin * 2));
		return screenRect;
	}

	void resetFocus()
	{
		camera.setTargetCenter(Vec2::Zero());
	}

	void convertEditData(int receiptIndex)
	{
		EditedData newData;
		auto& data = receiptData[receiptIndex];

		for (const auto& [groupIndex, group] : Indexed(data.textGroup))
		{
			String goodsStr;
			String priceStr;
			String dateStr;
			String timeStr;
			double itemMinX = DBL_MAX;
			double itemMinY = DBL_MAX;
			double itemMaxX = -DBL_MAX;
			double itemMaxY = -DBL_MAX;
			double priceMinX = DBL_MAX;
			double priceMinY = DBL_MAX;
			double priceMaxX = -DBL_MAX;
			double priceMaxY = -DBL_MAX;
			for (const auto& [textIndex, text] : Indexed(group))
			{
				const auto type = data.textMarkType[Point(groupIndex, textIndex)];

				switch (type)
				{
				case MarkType::Unassigned:
					break;
				case MarkType::ShopName:
					newData.shopName += text.Description;
					break;
				case MarkType::Date:
					dateStr += text.Description;
					break;
				case MarkType::Time:
					timeStr += text.Description;
					break;
				case MarkType::Goods:
				{
					goodsStr += text.Description;
					const auto minMaxX = text.minMaxX();
					const auto minMaxY = text.minMaxY();
					itemMinX = Min(itemMinX, minMaxX.x);
					itemMinY = Min(itemMinY, minMaxY.x);
					itemMaxX = Max(itemMaxX, minMaxX.y);
					itemMaxY = Max(itemMaxY, minMaxY.y);
				}
				break;
				case MarkType::Price: [[fallthrough]];
				case MarkType::Number:
				{
					priceStr += text.Description;
					const auto minMaxX = text.minMaxX();
					const auto minMaxY = text.minMaxY();
					priceMinX = Min(priceMinX, minMaxX.x);
					priceMinY = Min(priceMinY, minMaxY.x);
					priceMaxX = Max(priceMaxX, minMaxX.y);
					priceMaxY = Max(priceMaxY, minMaxY.y);
				}
				break;
				case MarkType::Ignore:
					break;
				default:
					break;
				}
			}

			if (!dateStr.empty())
			{
				int32 year = 2024;
				int32 month = 1;
				int32 day = 1;

				if (dateStr.count(U'/') == 2)
				{
					auto arr = dateStr.split(U'/');

					if (1 <= arr.size())
					{
						if (auto opt = ParseIntOpt<int32>(arr[0], 10))
						{
							year = Clamp(opt.value(), 0, 9999);
						}
					}
					if (2 <= arr.size())
					{
						if (auto opt = ParseIntOpt<int32>(arr[1], 10))
						{
							month = Clamp(opt.value(), 1, 12);
						}
					}
					if (3 <= arr.size())
					{
						if (auto opt = ParseIntOpt<int32>(arr[2], 10))
						{
							day = Clamp(opt.value(), 1, 31);
						}
					}
				}
				else if (1 == dateStr.count(U'年'))
				{
					const auto yearIndex = dateStr.indexOf(U'年');
					const auto monthIndex = dateStr.indexOf(U'月');
					const auto dayIndex = dateStr.indexOf(U'日');

					if (yearIndex != String::npos)
					{
						if (auto opt = ParseIntOpt<int32>(dateStr.substrView(0, yearIndex), 10))
						{
							year = Clamp(opt.value(), 0, 9999);
						}

						if (monthIndex != String::npos)
						{
							if (auto opt = ParseIntOpt<int32>(dateStr.substrView(yearIndex + 1, monthIndex - yearIndex - 1), 10))
							{
								month = Clamp(opt.value(), 1, 12);
							}

							if (dayIndex != String::npos)
							{
								if (auto opt = ParseIntOpt<int32>(dateStr.substrView(monthIndex + 1, dayIndex - monthIndex - 1), 10))
								{
									day = Clamp(opt.value(), 1, 31);
								}
							}
						}
					}
				}

				newData.date = Date(year, month, day);
			}

			if (!timeStr.empty())
			{
				if (1 <= timeStr.count(U':'))
				{
					auto arr = timeStr.split(U':');

					if (1 <= arr.size())
					{
						if (auto opt = ParseIntOpt<int32>(arr[0], 10))
						{
							newData.hours = Clamp(opt.value(), 0, 23);
						}
					}
					if (2 <= arr.size())
					{
						if (auto opt = ParseIntOpt<int32>(arr[1], 10))
						{
							newData.minutes = Clamp(opt.value(), 0, 59);
						}
					}
				}
			}

			priceStr.remove(U'*').remove(U'¥');

			if (!goodsStr.empty())
			{
				size_t emptyIndex = -1;
				for (const auto& [i, item] : Indexed(newData.itemData))
				{
					if (item.isNameEmpty())
					{
						emptyIndex = i;
						break;
					}
				}

				if (emptyIndex == -1)
				{
					newData.itemData.emplace_back();
					emptyIndex = newData.itemData.size() - 1;
				}

				auto& itemData = newData.itemData[emptyIndex];

				itemData.name = goodsStr;

				if (itemMinX < itemMaxX && itemMinY < itemMaxY)
				{
					itemData.nameTexRegion = RectF(itemMinX, itemMinY, itemMaxX - itemMinX, itemMaxY - itemMinY).asRect();
				}
			}

			if (!priceStr.empty())
			{
				int32 price = 0;
				if (auto opt = ParseIntOpt<int32>(priceStr))
				{
					price = opt.value();
				}

				if (price < 0)
				{
					// 空か直前のpriceが埋まっていたら新規追加
					if (newData.itemData.isEmpty() || !newData.itemData.back().discount.empty())
					{
						newData.itemData.emplace_back();
					}

					auto& itemData = newData.itemData.back();

					auto& discounts = itemData.discount;
					auto& discountRegion = itemData.discountTexRegion;

					discounts.push_back(price);

					if (priceMinX < priceMaxX && priceMinY < priceMaxY)
					{
						discountRegion.push_back(RectF(priceMinX, priceMinY, priceMaxX - priceMinX, priceMaxY - priceMinY).asRect());
					}
					else
					{
						discountRegion.push_back(Rect::Empty());
					}
				}
				else
				{
					size_t emptyIndex = -1;
					for (const auto& [i, item] : Indexed(newData.itemData))
					{
						if (item.isPriceEmpty())
						{
							emptyIndex = i;
							break;
						}
					}

					if (emptyIndex == -1)
					{
						newData.itemData.emplace_back();
						emptyIndex = newData.itemData.size() - 1;
					}

					auto& itemData = newData.itemData[emptyIndex];

					itemData.price = price;

					if (priceMinX < priceMaxX && priceMinY < priceMaxY)
					{
						itemData.priceTexRegion = RectF(priceMinX, priceMinY, priceMaxX - priceMinX, priceMaxY - priceMinY).asRect();
					}
				}
			}
		}

		newData.reloadCSV();
		editedData[receiptIndex] = newData;

		data.updatedMarkIndices.clear();
	}

	Array<ReceiptData> receiptData;
	HashTable<int, EditedData> editedData; // receiptIndex -> edited data
	int focusIndex = 0;
	double drawScale = 2.0;
	double iconDrawScale = 0.5;

	Font font = Font(10);
	Font mediumFont = Font(16);
	Font mediumFontBold = Font(18, Typeface::Bold);
	Font tableFont = Font(14);
	Font tableFontBold = Font(14, Typeface::Bold);
	Font largeFont = Font(17, Typeface::Bold);

	double rightAreaVerticalOffset = 0;

	double defaultCameraScale = 0.5;
	Camera2D camera = Camera2D{ Scene::Size() / 2, defaultCameraScale, CameraControl::RightClick };
	int marginX = 30;

	Optional<Vec2> dragStartPos;
	Optional<RectF> selectRange;

	Texture updateIcon = Texture(U"🔃"_emoji);
	Array<Texture> markIcons = { Texture(U"🏬"_emoji),Texture(U"📅"_emoji),Texture(U"🕰️"_emoji),Texture(U"🍔"_emoji),Texture(U"💴"_emoji),Texture(U"🧹"_emoji) };
	Array<MarkType> markTypes = { MarkType::ShopName, MarkType::Date, MarkType::Time, MarkType::Goods, MarkType::Price, MarkType::Unassigned };
	Array<Texture> writeIcons = { Texture(U"💾"_emoji),Texture(U"🗑️"_emoji) };
	Optional<MarkType> penType;

	bool showBoundingPoly = true;

	Vec2 buttonSize = Vec2(30, 30);
	Array<TileButton> markButtons =
	{
		TileButton{0xf54f_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::ShopName)]},
		TileButton{0xf133_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Date)]},
		TileButton{0xf017_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Time)]},
		TileButton{0xf805_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Goods)]},
		TileButton{0xf157_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Price)]},
		TileButton{0xf12d_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Unassigned)]},
	};

	TileButton saveButton = { 0xf0c7_icon, 15, Palette1, Palette::Skyblue };
	TileButton deleteButton = { 0xf1f8_icon, 15, Palette1, Palette::Skyblue };
	TileButton retryButton = { 0xf021_icon, 15, Palette1, Palette::Skyblue };

	TileButton showBoundingPolyButton = { 0xf247_icon, 15, Palette1, Palette::Skyblue, true };
	TileButton largerButton = { 0xf00e_icon, 15, Palette1, Palette::Skyblue };
	TileButton smallerButton = { 0xf010_icon, 15, Palette1, Palette::Skyblue };

	Font titleFont = Font(18);
};

void Main()
{
	Scene::SetBackground(Color{ 59, 59, 59 });
	Window::SetTitle(U"レシートOCR");
	Window::Resize(1920, 1080);

	ReceiptEditor editor;

	Texture tempTexture;
	int32 rotateNum = 0;

	String texturePath;

	while (System::Update())
	{
		if (DragDrop::HasNewFilePaths())
		{
			texturePath = DragDrop::GetDroppedFilePaths()[0].path;
			tempTexture = Texture(texturePath);
		}

		if (!tempTexture.isEmpty())
		{
			if (KeyRight.down())
			{
				++rotateNum;
			}

			const double rotateAngle = 90_deg * rotateNum;
			tempTexture.rotated(rotateAngle).draw();

			const auto saveFilePath = FileSystem::CurrentDirectory() + U"temp.jpg";
			if (KeyEnter.down())
			{
				switch (rotateNum % 4)
				{
				case 0: Image(texturePath).saveJPEG(saveFilePath, 100); break;
				case 1: Image(texturePath).rotate90().saveJPEG(saveFilePath, 100); break;
				case 2: Image(texturePath).rotate180().saveJPEG(saveFilePath, 100); break;
				case 3: Image(texturePath).rotate270().saveJPEG(saveFilePath, 100); break;
				default: break;
				}

				tempTexture = Texture();
				texturePath = saveFilePath;
				rotateNum = 0;
			}
		}
		else
		{
			editor.update();
			editor.draw();
		}

		if (KeyG.down() && !texturePath.empty())
		{
			tempTexture = Texture();
			rotateNum = 0;

			ChildProcess process(VisionExePath, texturePath, Pipe::StdIn);
			editor.calc(texturePath, process.istream());
		}
	}
}
