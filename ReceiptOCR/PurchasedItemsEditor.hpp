#pragma once
#include <Siv3D.hpp> // Siv3D v0.6.15
#include "Common.hpp"
#include "Utility.hpp"

struct TextEditor
{
	TextEditState state;
	Optional<MarkType> editType;
	size_t editIndex;
	size_t editIndex2;
};

struct EditDataBase
{
	bool isData = true;    // isData = false : 空欄を追加する用のダミーデータ
	bool isVisible = true; // isVivisble = false : 表示を消して保存時もスキップされる（復元可能な削除フラグ）

	size_t count() const
	{
		return isVisible ? 1 : 0;
	}
};

struct ItemNameEditData : public EditDataBase
{
	String name;
	Rect nameTexRegion = Rect::Empty();

	bool isNameTexEmpty() const
	{
		return nameTexRegion.isEmpty();
	}
};

struct ItemPriceEditData : public EditDataBase
{
	int32 price = 0;
	Rect priceTexRegion = Rect::Empty();

	bool isPriceTexEmpty() const
	{
		return priceTexRegion.isEmpty();
	}
};

struct ItemDiscountEditData : public EditDataBase
{
	Array<int32> discount;
	Array<Rect> discountTexRegion;

	bool isDiscountTexEmpty() const
	{
		return discountTexRegion.size() < discount.size();
	}
};

template<class EditDataType>
struct EditColumn
{
	size_t visibleCount() const
	{
		size_t count = 0;
		for (const auto& elem: data)
		{
			if (elem.isVisible)
			{
				++count;
			}
		}
		return count;
	}

	EditDataType* at(size_t index)
	{
		size_t dataIndex = 0;
		for (auto& elem : data)
		{
			if (elem.isVisible)
			{
				if (dataIndex == index)
				{
					return &elem;
				}

				++dataIndex;
			}
		}

		return nullptr;
	}

	const EditDataType* at(size_t index) const
	{
		size_t dataIndex = 0;
		for (auto& elem : data)
		{
			if (elem.isVisible)
			{
				if (dataIndex == index)
				{
					return &elem;
				}

				++dataIndex;
			}
		}

		return nullptr;
	}

	Optional<size_t> toPhysicalIndex(size_t visibleIndex) const
	{
		size_t dataIndex = 0;
		for (auto [physicalIndex, elem] : Indexed(data))
		{
			if (elem.isVisible)
			{
				if (dataIndex == visibleIndex)
				{
					return physicalIndex;
				}

				++dataIndex;
			}
		}

		return none;
	}

	Array<EditDataType> data;
};

class EditedData
{
public:

	String shopName;
	Date date;
	int32 hours = 0;
	int32 minutes = 0;
	EditColumn<ItemNameEditData> itemNameEdit;
	EditColumn<ItemPriceEditData> itemPriceEdit;
	EditColumn<ItemDiscountEditData> itemDiscountEdit;

	void editTextUpdate()
	{
		if (!textEdit.state.active)
		{
			conirmTextEdit();
			textEdit.editType = none;
		}
	}

	bool textEditing() const
	{
		return textEdit.editType.has_value();
	}

	void resetScroll()
	{
		gridScroll = 0;
	}

	size_t visibleRowCount() const
	{
		return Max({ itemNameEdit.visibleCount(),itemPriceEdit.visibleCount(),itemDiscountEdit.visibleCount() });
	}

	void makeTemporary()
	{
		temporaryData = makeDefaultTable();

		const auto idStr = id();
		const auto dateStr = buyDateFormat();
		const auto now = DateTime::Now();
		const auto nowStr = now.format(U"yyyy年MM月dd日HH時mm分ss秒");

		const auto rowCount = visibleRowCount();
		for (auto rowIndex : step(rowCount))
		{
			auto nameEditPtr = itemNameEdit.at(rowIndex);
			auto priceEditPtr = itemPriceEdit.at(rowIndex);
			auto discountEditPtr = itemDiscountEdit.at(rowIndex);

			int32 price = priceEditPtr ? priceEditPtr->price : 0;
			if (discountEditPtr)
			{
				for (const auto& v : discountEditPtr->discount)
				{
					price += v;
				}
			}

			const auto nameStr = nameEditPtr ? nameEditPtr->name : U"";
			const auto priceStr = Format(price);

			temporaryData.push_back_row({ nameStr, priceStr, shopName, dateStr }, { -1,1,0,0 });
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
				switch (textEdit.editIndex)
				{
				case 0:
					date.year = opt.value();
					break;
				case 1:
					date.month = opt.value();
					break;
				case 2:
					date.day = opt.value();
					break;
				case 3:
					hours = opt.value();
					break;
				case 4:
					minutes = opt.value();
					break;
				default:
					break;
				}
				reloadCSV();
			}
			break;
		case MarkType::Goods:
			if (auto nameEditPtr = itemNameEdit.at(textEdit.editIndex))
			{
				nameEditPtr->name = textEdit.state.text;
				makeTemporary();
			}
			break;
		case MarkType::Price: [[fallthrough]];
		case MarkType::Number:
			if (auto opt = ParseIntOpt<int32>(textEdit.state.text, 10))
			{
				if (textEdit.editIndex2 == 0)
				{
					if (auto priceEditPtr = itemPriceEdit.at(textEdit.editIndex))
					{
						priceEditPtr->price = opt.value();
					}
				}
				else
				{
					if (auto discountEditPtr = itemDiscountEdit.at(textEdit.editIndex))
					{
						discountEditPtr->discount[textEdit.editIndex2 - 1] = opt.value();
					}
				}
				makeTemporary();
			}
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
		const auto rowCount = visibleRowCount();
		if (rowCount == 0)
		{
			return RectF();
		}

		double sumOfTexHeight = 0.0;
		int32 numOfTexHeight = 0;
		for (auto rowIndex : step(rowCount))
		{
			if (auto nameEditPtr = itemNameEdit.at(rowIndex))
			{
				if (!nameEditPtr->isNameTexEmpty())
				{
					sumOfTexHeight += nameEditPtr->nameTexRegion.h;
					++numOfTexHeight;
				}
			}
			if (auto priceEditPtr = itemPriceEdit.at(rowIndex))
			{
				if (!priceEditPtr->isPriceTexEmpty())
				{
					sumOfTexHeight += priceEditPtr->priceTexRegion.h;
					++numOfTexHeight;
				}
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
		for (auto rowIndex : step(rowCount))
		{
			double maxHeight = 0;

			if (auto nameEditPtr = itemNameEdit.at(rowIndex); nameEditPtr && nameEditPtr->isData)
			{
				const auto nameStrRegion = font(WrapByWidth(font, nameEditPtr->name, maxNameWidth - xMargin)).region();
				const auto nameTexRegion = texture(nameEditPtr->nameTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).region();
				nameWidth.push_back(Max(nameStrRegion.w, nameTexRegion.w) + xMargin);//一番左端だけ空けないのでマージンは1つ分
				maxHeight = Max(maxHeight, nameStrRegion.h + nameTexRegion.h);
			}
			else // 末尾以降 or ダミー
			{
				nameWidth.push_back(0);
			}

			if (auto priceEditPtr = itemPriceEdit.at(rowIndex); priceEditPtr && priceEditPtr->isData)
			{
				const auto priceStrRegion = font(WrapByWidth(font, Format(priceEditPtr->price), maxPriceWidth - xMargin * 2)).region();
				const auto priceTexRegion = texture(priceEditPtr->priceTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).region();
				priceWidth.push_back(Max(priceStrRegion.w, priceTexRegion.w) + xMargin * 2);
				maxHeight = Max(maxHeight, priceStrRegion.h + priceTexRegion.h);

				sumOfPrice += priceEditPtr->price;
			}
			else // 末尾以降 or ダミー
			{
				priceWidth.push_back(0);
			}

			if (auto discountEditPtr = itemDiscountEdit.at(rowIndex); discountEditPtr && discountEditPtr->isData)
			{
				Array<double> currentWidth;

				for (const auto& [discountIndex, discount] : Indexed(discountEditPtr->discount))
				{
					const auto discountStrRegion = font(WrapByWidth(font, Format(discount), maxPriceWidth - xMargin * 2)).region();
					const auto discountTexRegion = texture(discountEditPtr->discountTexRegion[discountIndex].movedBy(-textureTopLeft)).scaled(drawScale).region();
					currentWidth.push_back(Max(discountStrRegion.w, discountTexRegion.w) + xMargin * 2);
					maxHeight = Max(maxHeight, discountStrRegion.h + discountTexRegion.h);

					sumOfPrice += discount;
					maxDiscountIndex = Max(maxDiscountIndex, discountIndex);
				}

				discountWidth.push_back(currentWidth);
			}
			else
			{
				Array<double> currentWidth;
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

			rect1 = drawEditableText(Format(hours), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Date, 3, 0, 30, isFocus);
			rect1 = font(U"時").draw(rect1.tr() + Vec2(10, 0), Palette::White);
			rect1 = drawEditableText(Format(minutes), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Date, 4, 0, 30, isFocus);
			rect1 = font(U"分").draw(rect1.tr() + Vec2(10, 0), Palette::White);
			pos_ = rect.bl() + Vec2(0, yMargin * 2);
		}

		Array<RectF> nameRects;
		Array<RectF> priceRects;
		Array<Array<RectF>> discountRects;

		for (auto itemIndex : step(rowCount))
		{
			const auto nameRect = RectF(pos_.x, pos_.y + sumHeight[itemIndex], calcMaxNameWidth, namePriceHeight[itemIndex]).stretched(-yMargin, -xMargin, -yMargin, 0);
			const auto priceRect = RectF(pos_.x + calcMaxNameWidth, pos_.y + sumHeight[itemIndex], calcMaxPriceWidth, namePriceHeight[itemIndex]).stretched(-xMargin, -yMargin);

			Array<RectF> currentRects;
			if (auto discountEditPtr = itemDiscountEdit.at(itemIndex); discountEditPtr && discountEditPtr->isData)
			{
				Array<double> currentWidth;

				for (const auto& [discountIndex, discount] : Indexed(discountEditPtr->discount))
				{
					const auto discountRect = RectF(
						pos_.x + calcMaxNameWidth + calcMaxPriceWidth,
						pos_.y + sumHeight[itemIndex],
						calcMaxDiscountWidth[discountIndex],
						namePriceHeight[itemIndex]).stretched(-xMargin, -yMargin);
					currentRects.push_back(discountRect);
				}
			}
			discountRects.push_back(currentRects);

			nameRects.push_back(nameRect);
			priceRects.push_back(priceRect);
		}

		struct EditRowOp
		{
			enum OperationType {Add, Remove};
			enum ColumnType { Name, Price, Discount };
			enum AddTo { Prev, Next };
			OperationType operationType;
			ColumnType columnType;
			AddTo addTo; // 追加時のみ使用
			size_t rowIndex = 0;
			size_t subIndex = 0; // Discountが複数ある場合そのインデックスを表す
		};

		Optional<EditRowOp> operationOpt;

		// 要素の削除
		bool guiHandled = false;
		if (!textEditing())
		{
			const double deleteButtonSize = 20;
			for (auto rowIndex : step(rowCount))
			{
				const RectF nameDeleteButton(Arg::topRight = nameRects[rowIndex].tr(), deleteButtonSize, nameRects[rowIndex].h);
				if (nameDeleteButton.mouseOver())
				{
					guiHandled = true;
					if (MouseL.down())
					{
						EditRowOp op;
						op.rowIndex = rowIndex;
						op.operationType = EditRowOp::OperationType::Remove;
						op.columnType = EditRowOp::ColumnType::Name;
						operationOpt = op;
					}
				}

				const RectF priceDeleteButton(Arg::topRight = priceRects[rowIndex].tr(), deleteButtonSize, priceRects[rowIndex].h);
				if (priceDeleteButton.mouseOver())
				{
					guiHandled = true;
					if (MouseL.down())
					{
						EditRowOp op;
						op.rowIndex = rowIndex;
						op.operationType = EditRowOp::OperationType::Remove;
						op.columnType = EditRowOp::ColumnType::Price;
						operationOpt = op;
					}
				}
			}
		}

		if (showSum)
		{
			const auto priceRect = RectF(pos_.x + calcMaxNameWidth, pos_.y + sumHeight[rowCount] + yMargin * 2, calcMaxPriceWidth, namePriceHeight[rowCount]).stretched(-xMargin, -yMargin);
			priceRects.push_back(priceRect);
		}

		isFocus = !guiHandled;

		for (auto itemIndex : step(rowCount))
		{
			const auto& namePos = nameRects[itemIndex].pos;
			if (auto nameEditPtr = itemNameEdit.at(itemIndex); nameEditPtr && nameEditPtr->isData)
			{
				auto nameRect = drawEditableText(nameEditPtr->name, font, namePos, Palette::Lime.withAlpha(255), MarkType::Goods, itemIndex, 0, calcMaxNameWidth - xMargin, isFocus);
				auto nameTexRect = texture(nameEditPtr->nameTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).draw(nameRect.bl() + Vec2(0, yInnerMargin));
			}

			const auto pricePos = priceRects[itemIndex].pos;
			if (auto priceEditPtr = itemPriceEdit.at(itemIndex); priceEditPtr && priceEditPtr->isData)
			{
				auto priceRect = drawEditableText(Format(priceEditPtr->price), font, pricePos, Palette::Cyan.withAlpha(255), MarkType::Price, itemIndex, 0, calcMaxPriceWidth - xMargin * 2, isFocus);
				auto priceTexRect = texture(priceEditPtr->priceTexRegion.movedBy(-textureTopLeft)).scaled(drawScale).draw(priceRect.bl() + Vec2(0, yInnerMargin));
			}

			if (auto discountEditPtr = itemDiscountEdit.at(itemIndex); discountEditPtr && discountEditPtr->isData)
			{
				for (const auto& [discountIndex, discount] : Indexed(discountEditPtr->discount))
				{
					const auto& rect = discountRects[itemIndex][discountIndex];
					auto discountRect = drawEditableText(Format(discount), font, rect.pos, Palette::Cyan.withAlpha(255), MarkType::Price, itemIndex, 1 + discountIndex, calcMaxDiscountWidth[discountIndex] - xMargin * 2, isFocus);
					auto discountTexRect = texture(discountEditPtr->discountTexRegion[discountIndex].movedBy(-textureTopLeft)).scaled(drawScale).draw(discountRect.bl() + Vec2(0, yInnerMargin));
				}
			}
		}

		if (showSum)
		{
			Line(pos0.x + xOuterMargin, nameRects.back().bottomY() + yMargin * 2, pos0.x + xOuterMargin + sumOfWidth, nameRects.back().bottomY() + yMargin * 2).draw();
			const auto sumRect = font(sumOfPrice).draw(priceRects.back().pos);
		}

		// 要素の削除の描画
		if (!textEditing())
		{
			const double deleteButtonSize = 20;
			for (auto rowIndex : step(rowCount))
			{
				const RectF nameDeleteButton(Arg::topRight = nameRects[rowIndex].tr(), deleteButtonSize, nameRects[rowIndex].h);
				if (nameDeleteButton.mouseOver())
				{
					nameRects[rowIndex].stretched(1).draw(Arg::left = Palette::Orangered.withA(0), Arg::right = Palette::Orangered);
				}

				const RectF priceDeleteButton(Arg::topRight = priceRects[rowIndex].tr(), deleteButtonSize, priceRects[rowIndex].h);
				if (priceDeleteButton.mouseOver())
				{
					priceRects[rowIndex].stretched(1).draw(Arg::left = Palette::Orangered.withA(0), Arg::right = Palette::Orangered);
				}
			}
		}

		// 要素の追加
		if (!textEditing())
		{
			auto intermedialPos = pos_ + Vec2(0, -yMargin);
			for (auto itemIndex : step(rowCount + 1))
			{
				if (1 <= itemIndex)
				{
					intermedialPos.y += namePriceHeight[itemIndex - 1];
				}

				RectF nameIntermedialRect(calcMaxNameWidth, yMargin * 2);

				const auto nameRect = RectF(intermedialPos.x, intermedialPos.y, calcMaxNameWidth, yMargin * 2).stretched(0, -xMargin, 0, 0);
				const auto priceRect = RectF(intermedialPos.x + calcMaxNameWidth, intermedialPos.y, calcMaxPriceWidth, yMargin * 2).stretched(-xMargin, 0);
				const auto bothRect = RectF(Arg::topRight = nameRect.tr(), nameRect.w * 0.5, nameRect.h);
				const auto bothRectDraw = RectF(nameRect.pos, calcMaxNameWidth + calcMaxPriceWidth, nameRect.h);

				if (bothRect.mouseOver())
				{
					TextureAsset(U"AddIcon").drawAt(bothRectDraw.center());
					bothRectDraw.stretched(0, -3).top().draw(LineStyle::SquareDot, 1);
					bothRectDraw.stretched(0, -3).bottom().draw(LineStyle::SquareDot, 1);
				}
				else if (nameRect.mouseOver())
				{
					TextureAsset(U"AddIcon").drawAt(nameRect.center());
					nameRect.stretched(0, -3).top().draw(LineStyle::SquareDot, 1);
					nameRect.stretched(0, -3).bottom().draw(LineStyle::SquareDot, 1);
				}
				else if (priceRect.mouseOver())
				{
					TextureAsset(U"AddIcon").drawAt(priceRect.center());
					priceRect.stretched(0, -3).top().draw(LineStyle::SquareDot, 1);
					priceRect.stretched(0, -3).bottom().draw(LineStyle::SquareDot, 1);
				}
			}
		}

		if (operationOpt)
		{
			const auto& operation = operationOpt.value();

			if (operation.operationType == EditRowOp::OperationType::Remove)
			{
				switch (operation.columnType)
				{
				case EditRowOp::Name:
					if (auto itemNamePtr = itemNameEdit.at(operation.rowIndex))
					{
						itemNamePtr->isVisible = false;
					}
					break;
				case EditRowOp::Price:
					if (auto itemPricePtr = itemPriceEdit.at(operation.rowIndex))
					{
						itemPricePtr->isVisible = false;
					}
					break;
				case EditRowOp::Discount:
					if (auto itemDiscountPtr = itemDiscountEdit.at(operation.rowIndex))
					{
						itemDiscountPtr->isVisible = false;
					}
					break;
				default:
					break;
				}
			}

			operationOpt = none;
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

		const auto rowCount = visibleRowCount();
		for (auto rowIndex : step(rowCount))
		{
			String nameStr;
			if (auto nameEditPtr = itemNameEdit.at(rowIndex))
			{
				nameStr = nameEditPtr->name;
			}

			int32 price = 0;
			if (auto priceEditPtr = itemPriceEdit.at(rowIndex))
			{
				price = priceEditPtr->price;
			}

			if (auto discountEditPtr = itemDiscountEdit.at(rowIndex))
			{
				for (const auto& discount : discountEditPtr->discount)
				{
					price += discount;
				}
			}

			String priceStr = Format(price);

			csv.writeRow(nameStr, priceStr, shopName, dateStr, nowStr, idStr);
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

	void drawGrid(const RectF& editRect, int marginX, int32 leftMargin, int32 topMargin, const Font& largeFont, const Vec2& buttonSize)
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
				gridScroll += Mouse::Wheel() * -50.0;
			}

			gridScroll = Min(0.0, gridScroll);

			Vec2 pos = textRect2.pos + Vec2(0, gridScroll);
			{
				const auto titleFontRegion = largeFont(U" 現在の編集データ ").draw(pos);
				pos = titleFontRegion.bl() + Vec2(0, 10);

				const auto region2 = temporaryData.region(pos);

				if (auto updated = saveButton.update(RectF(Arg::leftCenter = titleFontRegion.rightCenter(), buttonSize)))
				{
					if (updated.value())
					{
						writeData();
						reloadCSV();
						saveButton.lateRelease();
					}
				}

				temporaryData.draw(pos);
				pos = region2.bl() + Vec2(0, 30);
			}

			if (tableDataList.empty())
			{
				const auto region2 = largeFont(csvPath(), U" > ", U" 未登録データ").draw(pos);
				pos = region2.bl() + Vec2(0, 10);
			}
			else
			{
				const auto region2 = largeFont(buyDateFormat(), U" 購入分のレシート記録：", tableDataList.size(), U"件").draw(pos);
				pos = region2.bl() + Vec2(0, 10);
			}

			Optional<String> deleteRegisterDate;
			for (const auto& [dataIndex, data] : Indexed(tableDataList))
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
			}

			if (deleteRegisterDate)
			{
				deleteByRegisterDate(deleteRegisterDate.value());
				reloadCSV();
			}
		}

		textRect2_.drawFrame();
	}

private:

	OrderedTable<String, SimpleTable, Greater<String>> tableDataList; // 登録日時→登録データ
	SimpleTable temporaryData;
	Array<Texture> writeIcons = { Texture(U"💾"_emoji),Texture(U"🗑️"_emoji) };
	TileButton saveButton = { 0xf0c7_icon, 15, Palette1, Palette::Skyblue };
	TileButton deleteButton = { 0xf1f8_icon, 15, Palette1, Palette::Skyblue };
	double gridScroll = 0;
	TextEditor textEdit;
};