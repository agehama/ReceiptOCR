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
	Texture updateIcon = Texture(U"🔃"_emoji);
	Array<Texture> writeIcons = { Texture(U"💾"_emoji),Texture(U"🗑️"_emoji) };
	TileButton saveButton = { 0xf0c7_icon, 15, Palette1, Palette::Skyblue };
	TileButton deleteButton = { 0xf1f8_icon, 15, Palette1, Palette::Skyblue };

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

			rect1 = drawEditableText(Format(hours), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Date, 3, 0, 30, isFocus);
			rect1 = font(U"時").draw(rect1.tr() + Vec2(10, 0), Palette::White);
			rect1 = drawEditableText(Format(minutes), font, rect1.tr() + Vec2(10, 0), Palette::Greenyellow.withAlpha(255), MarkType::Date, 4, 0, 30, isFocus);
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

		if (!textEdit.editType)
		{
			auto intermedialPos = pos_ + Vec2(0, -yMargin);

			for (const auto itemIndex : step(itemData.size() + 1))
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

	void drawGrid(const RectF& editRect, int marginX, int32 leftMargin, int32 topMargin, double rightAreaVerticalOffset, const Font& largeFont, const Vec2& buttonSize)
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
								writeData();
								reloadCSV();
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
				deleteByRegisterDate(deleteRegisterDate.value());
				reloadCSV();
			}
		}

		textRect2_.drawFrame();
	}
};