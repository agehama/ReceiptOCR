#pragma once
#include <iostream>
#include <vector>
#include <numeric>
#include <Siv3D.hpp> // Siv3D v0.6.15

template <class T>
struct Greater
{
	inline bool operator()(const T& a, const T& b) const
	{
		return std::greater<T>()(a, b);
	}
};

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

inline String WrapByWidth(const Font& font, const String& str, double width)
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

class TileButton
{
public:

	struct Palette
	{
		ColorF tileColor1;
		ColorF tileColor2;
		ColorF borderColor1;
		ColorF borderColor2;
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
