<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="html" encoding="utf-8"/>

  <xsl:template match="t">
    <li>
      <xsl:choose>
        <xsl:when test="@href">
          <a href="{@href}"><xsl:value-of select="@name"/></a>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:if test="t">
        <ul>
          <xsl:apply-templates select="t"/>
        </ul>
      </xsl:if>
    </li>
  </xsl:template>

  <xsl:template match="/toc">
    <html>
      <head>
        <meta charset="utf-8"/>
        <link rel="stylesheet" href="layout.css"/>
        <title>Altirra Help</title>
      </head>
      <body>
        <div class="header">
          <div class="header-themetoggle">
            <a href="javascript:toggle_theme();">Toggle theme</a>
          </div>
          <div class="header-banner">Altirra Help</div>
          <div class="header-topic">Table of Contents</div>
        </div>
        <div class="main">
          <ul>
            <xsl:apply-templates select="t"/>
          </ul>
        </div>
        <script>
          if (window.location.hash == "#dark-theme")
            window.name = "dark";
          else if (window.location.hash == "#light-theme")
            window.name = "";

          function toggle_theme(theme) {
            window.name = (window.name == "dark" ? "" : "dark");
            update_theme();
          }

          function update_theme() {
            if (window.name == "dark")
              document.body.className = "dark";
            else
              document.body.className = "";
          }

          update_theme();
        </script>
      </body>
    </html>
  </xsl:template>

  <xsl:template match="*|text()"/>
</xsl:stylesheet>
