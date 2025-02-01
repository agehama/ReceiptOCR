#include <Siv3D.hpp> // Siv3D v0.6.15
#include "Utility.hpp"
#include "Vision.hpp"
#include "PurchasedItemsEditor.hpp"

constexpr Color MarkColor[] =
{
	Color{ 204, 204, 204 },
	Palette::Orange,
	Palette::Magenta,
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
		const auto reg = UR"((\d\d\d\d)[年/](\d\d?)[月/](\d\d?)日?\(?[月火水木金土日]?\)?(\d\d)?[時:]?(\d\d)?)"_re;
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

struct Group
{
	Array<size_t> largeGroup;
	OrderedTable<size_t, Array<size_t>> smallGroup;
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

class ReceiptEditor
{
public:

	int32 windowMarginLR = 60;
	int32 windowMarginTB = 100;
	int viewIntervalX = 30;

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
		convertEditData(index);
		resetFocus();
	}

	void update()
	{
		if (receiptData.empty())
		{
			return;
		}

		if (editedData[focusIndex].textEditing())
		{
			editedData[focusIndex].editTextUpdate();
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
			editedData[focusIndex].resetScroll();
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

		if (!editedData[focusIndex].textEditing())
		{
			camera.update();
		}

		const auto markerRect = updateUI2();
		const double margin1 = 20;
		const Vec2 scopePos = markerRect.tr() + Vec2(margin1, 0);

		titleFont(focusIndex + 1, U" / ", receiptData.size(), U" 件目").draw(Arg::bottomLeft = scopePos - Vec2(0, 10));

		for (auto [receiptIndex, data] : IndexedRef(receiptData))
		{
			if (focusIndex != receiptIndex)
			{
				continue;
			}

			// 左のデバッグ描画
			drawMarkerView(scopePos, data);

			if (!data.updatedMarkIndices.empty())
			{
				convertEditData(receiptIndex);
			}

			auto scope = getScreenScope(scopePos);
			RectF textRect = RectF(Arg::topLeft = scope.tr() + Vec2(viewIntervalX, 0), 500 * drawScale, 50 * drawScale);

			if (editedData.contains(receiptIndex))
			{
				auto& editData = editedData.at(receiptIndex);

				// 中央のレシート読み取り結果
				const auto editRect = editData.draw(textRect.pos, mediumFont, data.texture, data.topLeft.asPoint(), camera.getTargetScale(), receiptIndex == focusIndex);
				RectF(editRect.pos, editRect.w, Scene::Height() - windowMarginTB * 2).drawFrame();

				// 右の表
				if (!editRect.isEmpty())
				{
					editData.drawGrid(editRect, viewIntervalX, windowMarginLR, windowMarginTB, largeFont, buttonSize);
				}
			}
		}

		getScreenScope(scopePos).drawFrame();
	}

	void drawMarkerView(const Vec2& scopePos, ReceiptData& data)
	{
		auto scope = getScreenScope(scopePos);

		const auto receiptAreaMouseOver = scope.mouseOver();

		auto t1 = Transformer2D(Mat3x2::Translate((scope.center() - Scene::CenterF()) / camera.getScale()), TransformCursor::Yes);
		scope.pos = Vec2::Zero();

		const auto tex = data.texture.scaled(drawScale);

		RectF textRect = RectF(Arg::topLeft = scope.tr() + Vec2(viewIntervalX, 0), 500 * drawScale, 50 * drawScale);

		auto t2 = camera.createTransformer();

		Graphics2D::SetScissorRect(getScreenScope(scopePos).asRect());
		RasterizerState rs = RasterizerState::Default2D;
		rs.scissorEnable = true;
		const ScopedRenderStates2D rasterizer{ rs };

		tex.drawAt(Vec2::Zero());

		if (showBoundingPoly)
		{
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
		}

		camera.draw();
	}

	RectF updateUI2()
	{
		const Vec2 pos(windowMarginLR, windowMarginTB);

		const double margin1 = 20;
		const double margin2 = 10;

		auto rect1 = RectF(pos, buttonSize);
		auto rect2 = rect1.movedBy(rect1.w + margin2, 0);
		auto rect3 = rect1.movedBy(0, rect1.h + margin2);
		auto rect4 = rect2.movedBy(0, rect1.h + margin2);
		auto rect5 = rect3.movedBy(0, rect1.h + margin2);
		auto rect6 = rect4.movedBy(0, rect1.h + margin2);
		Optional<size_t> selected;

		Array<RectF> rects = { rect1,rect2,rect3,rect4,rect5 };
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
						//data.image[y][x] = Color{ 59, 59, 59 };
						data.image[y][x] = Color{0,0,0,0};
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

	RectF getScreenScope(const Vec2 pos) const
	{
		const auto sceneRect = Scene::Rect();
		const RectF screenRect(pos, (sceneRect.w / 4.0), (sceneRect.h - windowMarginTB * 2));
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
			String dateTimeStr;
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
					dateTimeStr += text.Description;
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

			if (!dateTimeStr.empty())
			{
				Array<int32> dateTime = { 2024,1,1,0,0 };

				const auto reg = UR"((\d\d\d\d)[年/](\d\d?)[月/](\d\d?)日?\(?[月火水木金土日]?\)?(\d\d)?[時:]?(\d\d)?)"_re;

				const auto result = reg.search(dateTimeStr);
				for (size_t i = 1; i < result.size(); ++i)
				{
					if (result[i])
					{
						if (auto opt = ParseIntOpt<int32>(result[i].value(), 10))
						{
							dateTime[i - 1] = opt.value();
						}
					}
				}

				newData.date = Date(dateTime[0], dateTime[1], dateTime[2]);
				newData.hours = dateTime[3];
				newData.minutes = dateTime[4];
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

	double defaultCameraScale = 0.5;
	Camera2D camera = Camera2D{ Scene::Size() / 2, defaultCameraScale, CameraControl::RightClick };

	Optional<Vec2> dragStartPos;
	Optional<RectF> selectRange;

	Texture updateIcon = Texture(U"🔃"_emoji);
	Array<Texture> markIcons = { Texture(U"🏬"_emoji),Texture(U"🕰️"_emoji),Texture(U"🍔"_emoji),Texture(U"💴"_emoji),Texture(U"🧹"_emoji) };
	Array<MarkType> markTypes = { MarkType::ShopName, MarkType::Date, MarkType::Goods, MarkType::Price, MarkType::Unassigned };
	Array<Texture> writeIcons = { Texture(U"💾"_emoji),Texture(U"🗑️"_emoji) };
	Optional<MarkType> penType;

	bool showBoundingPoly = true;

	Vec2 buttonSize = Vec2(30, 30);
	Array<TileButton> markButtons =
	{
		TileButton{0xf54f_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::ShopName)]},
		TileButton{0xf017_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Date)]},
		TileButton{0xf805_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Goods)]},
		TileButton{0xf157_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Price)]},
		TileButton{0xf12d_icon, 15, Palette1, MarkColor[static_cast<size_t>(MarkType::Unassigned)]},
	};

	TileButton retryButton = { 0xf021_icon, 15, Palette1, Palette::Skyblue };

	TileButton showBoundingPolyButton = { 0xf247_icon, 15, Palette1, Palette::Skyblue, true };
	TileButton largerButton = { 0xf00e_icon, 15, Palette1, Palette::Skyblue };
	TileButton smallerButton = { 0xf010_icon, 15, Palette1, Palette::Skyblue };

	Font titleFont = Font(18);
};

#define TEST
//#define TEST_DUMP

#ifdef TEST
#include <fstream>
void DumpResult(const std::string& dumpPath, std::istream& is)
{
	std::ofstream ofs;
	ofs.open(dumpPath, std::ios::binary);
	std::string line;
	while (std::getline(is, line))
	{
		if (line.ends_with('\r'))
		{
			line.pop_back();
		}
		ofs << line << '\n';
	}
}
#endif

void LoadConfig(FilePathView configPath, ReceiptEditor& editor)
{
	INI ini(configPath);
	if (not ini)
	{
		throw Error{ U"Failed to load `config.ini`" };
	}

	const auto windowWidth = Parse<int32>(ini[U"Window.width"]);
	const auto windowHeight = Parse<int32>(ini[U"Window.height"]);
	const auto currentSize = Scene::Size();
	if (currentSize.x != windowWidth || currentSize.y != windowHeight)
	{
		Window::Resize(windowWidth, windowHeight);
	}

	const auto windowColor = Parse<Color>(ini[U"Window.background"]);
	if (Scene::GetBackground().toColor() != windowColor)
	{
		Scene::SetBackground(windowColor);
	}

	const auto windowMarginX = Parse<int32>(ini[U"Window.windowMarginX"]);
	const auto windowMarginY = Parse<int32>(ini[U"Window.windowMarginY"]);
	const auto viewIntervalX = Parse<int32>(ini[U"Window.viewIntervalX"]);
	editor.windowMarginLR = windowMarginX;
	editor.windowMarginTB = windowMarginY;
	editor.viewIntervalX = viewIntervalX;
}

void Main()
{
	const auto configPath = U"config/config.ini";
	const auto fullConfigPath = FileSystem::FullPath(configPath);
	const auto configDirectory = FileSystem::ParentPath(fullConfigPath);

	DirectoryWatcher watcher{ configDirectory };

	TextureAsset::Register(U"AddIcon", 0xf0704_icon, 12);

	Window::SetTitle(U"レシートOCR");
	Window::SetStyle(WindowStyle::Sizable);

	ReceiptEditor editor;
	Texture tempTexture;
	int32 rotateNum = 0;
	String texturePath;

#ifdef TEST
	const auto dumpPath = "test/dump.txt";
	texturePath = U"test/test01.jpg";
#ifdef TEST_DUMP
	// CloudVision.exeからの相対パス
	const auto relativePath = FileSystem::RelativePath(texturePath, FileSystem::ParentPath(VisionExePath));
	ChildProcess process(VisionExePath, relativePath, Pipe::StdIn);
	auto& is = process.istream();
	DumpResult(dumpPath, is);
	return;
#else
	std::ifstream is(dumpPath);
	editor.calc(texturePath, is);
#endif
#endif

	LoadConfig(configPath, editor);

	while (System::Update())
	{
		for (auto&& [path, action] : watcher.retrieveChanges())
		{
			if (path == fullConfigPath && action == FileAction::Modified)
			{
				try
				{
					LoadConfig(configPath, editor);
				}
				catch (std::exception& e)
				{
					Print << U"iniファイルが無効です";
				}
			}
		}

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
